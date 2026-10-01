"""Parts more than one project in this volume needs.

The codec twin, the edge-rate analysis and the serialisation-size arithmetic are
not properties of P09 or P06. P08 builds the frame P09 encodes, P12 tests it on a
runner with no board, P11 sends it over a modem, and P06's rate analysis is the
instrument P02, P03 and P18 all reach for. Each of those wants most of this, so
it lives here rather than in the first project that happened to need it.

Nothing here touches hardware. That is the rule for this whole repository: if a
module needs a board, a serial port or a Raspberry Pi, it belongs in a project
directory and not in this package.
"""

__version__ = "0.1.0"
