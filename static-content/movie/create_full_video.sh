#!/bin/sh

ffmpeg -y -loop 1 -i transparent-16x16.png -t 28800 -r 1 -c:v vp8 -auto-alt-ref 0 transparent-full.mp4