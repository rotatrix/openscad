"""Exercise bundled multimedia through the AppImage's embedded Python."""
import ctypes
import os
import struct
import wave
from pathlib import Path

from openscad import cube, show

appdir = Path(os.environ["APPDIR"])
gstreamer = ctypes.CDLL(str(appdir / "usr/lib/libgstreamer-1.0.so.0"))


def bind(name, result_type, *argument_types):
    function = getattr(gstreamer, name)
    function.restype = result_type
    function.argtypes = argument_types
    return function


pointer = ctypes.c_void_p
bind("gst_init", None, pointer, pointer)(None, None)
find_factory = bind("gst_element_factory_find", pointer, ctypes.c_char_p)
for element in (
    b"audiotestsrc", b"audioconvert", b"audioresample", b"wavparse",
    b"wavenc", b"playbin", b"pulsesink",
):
    assert find_factory(element), f"Missing multimedia element: {element!r}"


class List(ctypes.Structure):
    pass


List._fields_ = [
    ("data", pointer),
    ("next", ctypes.POINTER(List)),
    ("prev", ctypes.POINTER(List)),
]
registry = bind("gst_registry_get", pointer)()
get_plugins = bind("gst_registry_get_plugin_list", ctypes.POINTER(List), pointer)
get_filename = bind("gst_plugin_get_filename", ctypes.c_char_p, pointer)
get_description = bind("gst_plugin_get_description", ctypes.c_char_p, pointer)
node = get_plugins(registry)
count = 0
while node:
    filename = get_filename(node.contents.data)
    if filename:
        assert Path(os.fsdecode(filename)).is_relative_to(appdir), f"Host plugin: {filename!r}"
        description = get_description(node.contents.data) or b""
        assert b"blacklisted" not in description.lower(), f"Broken plugin: {filename!r}"
        count += 1
    node = node.contents.next
assert count > 20, "Incomplete multimedia plugin registry"

# Decode and convert actual WAV data without requiring an audio device in CI.
wav = Path(os.environ["PROBE_WAV"])
with wave.open(str(wav), "wb") as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(8000)
    output.writeframes(struct.pack("<h", 0) * 800)
parse = bind("gst_parse_launch", pointer, ctypes.c_char_p, pointer)
pipeline = parse(
    f'filesrc location="{wav}" ! wavparse ! audioconvert ! audioresample ! fakesink'.encode(),
    None,
)
assert pipeline, "Could not create WAV decoding pipeline"
set_state = bind("gst_element_set_state", ctypes.c_int, pointer, ctypes.c_int)
assert set_state(pipeline, 4) != 0, "Could not start pipeline"  # GST_STATE_PLAYING
bus = bind("gst_element_get_bus", pointer, pointer)(pipeline)
wait_for_eos = bind("gst_bus_timed_pop_filtered", pointer, pointer, ctypes.c_uint64, ctypes.c_int)
try:
    assert wait_for_eos(bus, 10_000_000_000, 1), "WAV playback did not reach EOS"
finally:
    set_state(pipeline, 1)  # GST_STATE_NULL
print(f"MULTIMEDIA PASS: {count} bundled plugins; WAV decode/convert/resample reached EOS")
show(cube(1))
