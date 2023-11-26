## Remotetranscoder
An application which works together with [cefbrowser](https://github.com/Zabrimus/cefbrowser) and [vdr-plugin-web](https://github.com/Zabrimus/vdr-plugin-web) to show HbbTV application and stream videos.

# Build
```
meson setup build
cd build
meson compile
```
The binary exists in the build folder with name ```remotetrans```.

# Configuration
A default configuration can be found in folder config: ```sockets.ini```.

:fire: All ports/ip addresses in ```sockets.ini``` must be the same as for ```cefbrowser``` and ```vdr-plugin-web```.
It's safe to use the same sockets.ini for all of the three parts (vdr-plugin-web, cefbrowser, remotetranscoder). 

# Parameters
```
-c / --config </path/to/sockets.ini>   (mandatory parameter)
-t / --transcode </path/to/codecs.ini> (optional parameter)
-m / --movie <directory of the movie files, default ./movie> (optional parameter)
-l / --loglevel <level> (from 0 to 4, where 4 means very, very verbose logging)
-s / --seekPause <ms> (wait ms milliseconds before really doing a video jump. In the meantime all jumps are collected)
```

# Logging
Log entries will be written to stdout/stderr.

# HTTP commands (used by the cefbrowser)
## /Probe
### Parameters
> url (the video URL)
> 
> cookies (the browser cookies)
> 
> referer (the http referer)
> 
> userAgent (the browser user agent)
> 
> responseIp (the ip of the browser http server)
> 
> responsePort (the port of the browser http server)
> 
> vdrIp (the ip of the VDR http server)
> 
> vdrPort (the port of the VDR http server)
> 
> postfix (random string)

Calls FFprobe of the URL to determine codecs and video length. Prepares internally the video stream.

## /StreamUrl
### Parameters
> url (the video URL)
>
> cookies (the browser cookies)
>
> referer (the http referer)
>
> userAgent (the browser user agent)
>
> responseIp (the ip of the browser http server)
>
> responsePort (the port of the browser http server)
>
> vdrIp (the ip of the VDR http server)
>
> vdrPort (the port of the VDR http server)

Must be called after ```/Probe``` and really starts the video streaming. The TS packets are send directly to VDR.

## /Pause
### Parameters
> streamId (id of the stream, created by /Probe)

Pause the video stream.

## /SeekTo
### Parameters
> streamId (id of the stream, created by /Probe)
> 
> seekTo (String containing the timestamp)

Jumps to another position in the video stream.


## /Resume
### Parameters
> streamId (id of the stream, created by /Probe)

Resumes the video streaming, called after ```/Pause```.

## /Stop
### Parameters
> streamId (id of the stream, created by /Probe)

Stops the video streaming.

## /movie/(.*)
Returns the created transparent video file.
