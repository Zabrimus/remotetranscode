# Remotetranscoder
An application which works together with [cefbrowser](https://github.com/Zabrimus/cefbrowser) and [vdr-plugin-web](https://github.com/Zabrimus/vdr-plugin-web) to show HbbTV application and stream videos.

## Build
```
meson setup build
cd build
meson compile
meson install
```
The release exists in the subdirectory ```Release``` of the build folder.

With ```meson setup -Drelease_dir=false build``` the binaries are installed into the ```bindir``` of the prefix
and the movie files into ```<datadir>/remotetranscode/movie```, e.g. ```/usr/local/share/remotetranscode/movie```.
Then start remotrans with ```-m``` and this directory.

## Configuration
A default configuration can be found in folder config: ```sockets.ini```.

:fire: All ports/ip addresses in ```sockets.ini``` must be the same as for ```cefbrowser``` and ```vdr-plugin-web```.
It's safe to use the same sockets.ini for all of the three parts (vdr-plugin-web, cefbrowser, remotetranscoder). 

## Parameters
```
-c / --config </path/to/sockets.ini>   (mandatory parameter)
-t / --transcode </path/to/codecs.ini> (optional parameter)
-m / --movie <directory of the movie files, default ./movie (optional parameter)
-l / --loglevel <level> (from 0 to 4, where 4 means very, very verbose logging)
-s / --seekPause <ms> (wait ms milliseconds before really doing a video jump. In the meantime all jumps are collected)
-a / --enablekodi (use Kodi inputstream-adaptive for mpeg dash (Very experimental!)
-k / --kodipath The path to inputstream-adaptive. (Default: /.storage/.kodi) 
-d / --downloaddir <directory> (enables downloads of videos as MP4 or TS into this directory)
-w / --mediathek <url> (the MediathekViewWeb server, default https://mediathekviewweb.de)
```

## Downloads
With ```-d``` vdr-plugin-web can download the current video as MP4 or TS. The transcoder looks up
the video in [MediathekViewWeb](https://mediathekviewweb.de) by channel and duration, the
title of the page and the EPG of the channel help to sort the entries. Their resolution and size
are probed, a maximum height selects a lower quality. An entry found there is downloaded as file
of the broadcaster with a name of channel, topic, title and date. Otherwise the stream itself is
saved. Downloads run in the background, independent of the playback, more than the given number
wait in a queue. A download needs the free space given by the plugin and its estimated size.

Incomplete files end with ```.part```. The data of a download is kept in a hidden file
```.<name>.json``` next to it: a failed or interrupted download can be started again, also after
a restart. A MP4 file can be converted to TS, the MP4 file is deleted afterwards.

## Logging
Log entries will be written to stdout/stderr.

## Test ffmpeg/ffprobe
It's a good idea, to check if ffmpeg and ffprobe exists and that all necessary libraries exists.
> cd build/Release (or /usr/local/share/remotetranscode with release_dir=false)
>
> ffprobe movie/transparent-full.webm
>
> ffmpeg -i movie/transparent-full.webm -t 10 -codec copy -f webm /tmp/please_remove_me.mp4
>
> rm /tmp/please_remove_me.mp4

Both ffprobe and ffmpeg shall throw no errors.
