#!/bin/bash
# Record all outputs of the Weston PipeWire backend into a single video.
#
# Each enabled output of the PipeWire backend appears as a video stream
# node named "weston.<output-name>" (see libweston/backend-pipewire).
# This script discovers all of them, composites them side by side in
# node-name order, and encodes the result into one MP4 file.
#
# Usage: record-pipewire-outputs.sh [FILE]
#   FILE  output file, default weston-recording.mp4
# Stop the recording with Ctrl-C.
#
# Requires pw-dump, jq and gst-launch-1.0 with the pipewire, videoconvert,
# x264 and mp4mux elements.

set -eu

out="${1:-weston-recording.mp4}"

# One "<node.name> <width> <height>" line per enabled weston output node;
# width/height are fixed in the EnumFormat param the backend advertises.
nodes=$(pw-dump | jq -r '
	.[]
	| select(.type == "PipeWire:Interface:Node")
	| select(.info.props["node.name"] // "" | startswith("weston."))
	| . as $n
	| first($n.info.params.EnumFormat[]? | select(.size) | .size)
	| [$n.info.props["node.name"], .width, .height] | @tsv' | sort -u)

if [ -z "$nodes" ]; then
	echo "No Weston PipeWire output nodes found." >&2
	echo "Is weston running with --backend=pipewire?" >&2
	exit 1
fi

mix="compositor name=mix"
srcs=""
i=0
xpos=0
while IFS=$'\t' read -r name width height; do
	mix="$mix sink_$i::xpos=$xpos"
	srcs="$srcs pipewiresrc target-object=$name ! videoconvert ! queue ! mix.sink_$i"
	xpos=$((xpos + width))
	i=$((i + 1))
done <<< "$nodes"

echo "Recording $i output(s) to '$out', press Ctrl-C to stop:"
echo "$nodes"

exec gst-launch-1.0 -e $mix ! videoconvert ! x264enc ! h264parse ! \
	mp4mux ! filesink location="$out" $srcs
