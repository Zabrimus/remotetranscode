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
```

## Logging
Log entries will be written to stdout/stderr.

## Test ffmpeg/ffprobe
It's a good idea, to check if ffmpeg and ffprobe exists and that all necessary libraries exists.
> cd build/Release
>
> ffprobe movie/transparent-full.webm
>
> ffmpeg -i movie/transparent-full.webm -t 10 -codec copy -f webm please_remove_me.mp4
>
> rm please_remove_me.mp4

Both ffprobe and ffmpeg shall throw no errors.
