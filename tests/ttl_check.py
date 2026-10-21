# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos
"""Check parsed Turtle against the C++ port ABI. Test dependency: rdflib."""
import pathlib
import re
import sys

try:
    import rdflib
    from rdflib import Namespace, RDF, RDFS, URIRef
except ImportError:
    sys.exit("FAIL ttl_check: install python3-rdflib (or pip install rdflib)")

ROOT = pathlib.Path(__file__).resolve().parents[1]
LV2 = Namespace("http://lv2plug.in/ns/lv2core#")
DOAP = Namespace("http://usefulinc.com/ns/doap#")
UNITS = Namespace("http://lv2plug.in/ns/extensions/units#")
PLUGIN = URIRef("https://github.com/ABustos2606/chamber#chamber")
COUNT = 53
# Fixed in milestone 0: symbols never change and new ports are appended.
PERMANENT = """blow_in strike_in out_l out_r contour bow_level bow_timbre blow_level blow_meta
blow_timbre strike_level strike_meta strike_timbre geometry brightness damping position model
coarse fine fm_amount space play strength blow_in_level strike_in_level ominous output
bow_timbre_amount blow_meta_amount blow_timbre_amount strike_meta_amount strike_timbre_amount
geometry_amount brightness_amount damping_amount position_amount space_amount voct_cv fm_cv
strength_cv gate_cv bow_timbre_cv blow_meta_cv blow_timbre_cv strike_meta_cv strike_timbre_cv
geometry_cv brightness_cv damping_cv position_cv space_cv latency""".split()
FORBIDDEN = re.compile(r"mutable|elements|gillet|emilie", re.I)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(bundle):
    header = (ROOT / "src/ports.hpp").read_text(encoding="utf-8")
    body = re.search(r"enum class Port : uint32_t \{(.*?)\};", header, re.S)
    require(body is not None, "missing Port enum")
    declared = re.findall(r"(\w+)\s*=\s*(\d+),\s*//\s*(audio_in|audio_out|control_in|control_out|cv_in|cv_out)", body[1])
    expected = {int(index): (symbol, kind) for symbol, index, kind in declared}
    require(len(declared) == len(expected) == COUNT, "Port: count/duplicate index")
    require(set(expected) == set(range(COUNT)), "Port: non-contiguous indices")
    require(re.search(r"count\s*=\s*" + str(COUNT) + r"\b", body[1]), "Port: count sentinel")
    require([expected[i][0] for i in range(len(PERMANENT))] == PERMANENT, "permanent indices/symbols changed")

    manifest = rdflib.Graph().parse(bundle / "manifest.ttl", format="turtle")
    require(set(manifest.subjects(RDF.type, LV2.Plugin)) == {PLUGIN}, "manifest: one plugin")
    require(manifest.value(PLUGIN, LV2.binary) == URIRef((bundle / "chamber.so").as_uri()), "manifest: binary")
    require(manifest.value(PLUGIN, RDFS.seeAlso) == URIRef((bundle / "chamber.ttl").as_uri()), "manifest: seeAlso")

    graph = rdflib.Graph().parse(bundle / "chamber.ttl", format="turtle")
    require((PLUGIN, RDF.type, LV2.Plugin) in graph, "plugin class")
    # Peepo withholds an InstrumentPlugin as "played by MIDI notes" until it
    # is checked with a controller; Chamber has no MIDI input.
    require((PLUGIN, RDF.type, LV2.GeneratorPlugin) in graph and (PLUGIN, RDF.type, LV2.InstrumentPlugin) not in graph,
            "plugin class: a generator, not an instrument")
    require(graph.value(PLUGIN, DOAP.license) == URIRef("https://spdx.org/licenses/MIT"), "license")
    require(str(graph.value(PLUGIN, DOAP.name)) == "Chamber", "name")
    require(not FORBIDDEN.search(str(graph.value(PLUGIN, RDFS.comment))), "description names the original")
    require(not set(graph.objects(PLUGIN, LV2.requiredFeature)), "no required features")
    require(LV2.hardRTCapable in set(graph.objects(PLUGIN, LV2.optionalFeature)), "hardRTCapable")
    require(graph.value(PLUGIN, LV2.minorVersion) is not None and graph.value(PLUGIN, LV2.microVersion) is not None, "version")

    actual, symbols = {}, set()
    for port in graph.objects(PLUGIN, LV2.port):
        index, symbol, name = int(graph.value(port, LV2["index"])), str(graph.value(port, LV2.symbol)), str(graph.value(port, LV2.name))
        require(index not in actual and symbol not in symbols, f"duplicate port {symbol}")
        symbols.add(symbol)
        require(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol), f"invalid symbol: {symbol}")
        require(name.isascii() and name.isprintable() and 0 < len(name) <= 16, f"{symbol}: name must be ASCII, <=16 characters")
        require(not FORBIDDEN.search(name), f"{symbol}: name names the original")
        types = set(graph.objects(port, RDF.type))
        directions = types & {LV2.InputPort, LV2.OutputPort}
        kinds = types & {LV2.AudioPort, LV2.ControlPort, LV2.CVPort}
        require(len(directions) == len(kinds) == 1, f"{symbol}: ambiguous port kind")
        kind = {LV2.AudioPort: "audio", LV2.ControlPort: "control", LV2.CVPort: "cv"}[next(iter(kinds))]
        kind += "_in" if LV2.InputPort in types else "_out"
        actual[index] = (symbol, kind)
        lo, hi = graph.value(port, LV2.minimum), graph.value(port, LV2.maximum)
        if LV2.ControlPort in types or LV2.CVPort in types:
            require(lo is not None and hi is not None and float(lo) <= float(hi), f"{symbol}: bounds")
            if LV2.InputPort in types:
                default = graph.value(port, LV2.default)
                require(default is not None and float(lo) <= float(default) <= float(hi), f"{symbol}: default")
        props = set(graph.objects(port, LV2.portProperty))
        if LV2.enumeration in props:
            points = list(graph.objects(port, LV2.scalePoint))
            values = sorted(float(graph.value(p, RDF.value)) for p in points)
            require(LV2.integer in props and values == list(range(len(points))), f"{symbol}: enum values")
            require(values[0] == float(lo) and values[-1] == float(hi), f"{symbol}: enum bounds")
            require(all(str(graph.value(p, RDFS.label)).isascii() for p in points), f"{symbol}: labels")
        if symbol == "model":
            labels = {int(float(graph.value(p, RDF.value))): str(graph.value(p, RDFS.label)) for p in graph.objects(port, LV2.scalePoint)}
            require(labels == {0: "Modal", 1: "String", 2: "Strings"}, "model labels follow ResonatorModel")
        if symbol in ("play", "ominous"):
            require(LV2.toggled in props, f"{symbol}: toggled")
        if symbol == "coarse":
            require(LV2.integer in props and (float(lo), float(hi)) == (0, 60), "coarse: 5 octaves of semitones")
        if symbol == "latency":
            require(LV2.reportsLatency in props and graph.value(port, LV2.designation) == LV2.latency, "latency port")
        if symbol.endswith("_level") and symbol.startswith(("blow_in", "strike_in")) or symbol == "output":
            require(graph.value(port, UNITS.unit) == UNITS.db, f"{symbol}: dB")
    require(actual == expected, f"ports differ from Port: {actual}")
    print(f"PASS ttl_check: {COUNT} ports match ports.hpp, names, bounds, license and bundle")


if __name__ == "__main__":
    try:
        check(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "chamber.lv2").resolve())
    except Exception as error:
        sys.exit(f"FAIL ttl_check: {error}")
