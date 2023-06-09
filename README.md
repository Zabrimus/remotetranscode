# Warning
- highly instable 
- does not works as desired 
- could destroy your system 
- in development phase


## Remotetranscoder (Part 3)
Works together with ```cefbrowser``` and ```vdr-plugin-web```

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

# Start
```./remotrans -c /path/to/sockets.ini```

# Logging
Log entries will be written to stdout/stderr.