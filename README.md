# Starfield star system editor for ESP moding
Original creation by tridasha.

**EXPERIMENTAL** Windows GUI tool that lets you create a new star system with planets in an existing Starfield ESP.
C++ [Visual Studio 2022](https://visualstudio.microsoft.com/) project. If you don't want to install Visual Studio, you have the option of downloading a [Microsoft VM with it already installed](https://developer.microsoft.com/en-us/windows/downloads/virtual-machines/). 

Note: Requires zlib1.dll which can be installed with [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/overview) https://vcpkg.io/en/package/zlib when building. If you don't want to get it and build it with vckpg (which in itself is hard to setup) then there is a version of the dll in [nifscope](https://github.com/fo76utils/nifskope/releases/). Don't download it from a random site. You will also need the zlib.h for version zlib 1.2.13 to go with the dll. This can be found in the "Source code
(zip)" in the [zlib source on github](https://github.com/madler/zlib/releases/tag/v1.2.13). You just need the zlib.h from the zip and nothing else. You can put this in with the source code directory for this project with the code, but... you will need to change the `#include <zlib.h>` to `#include "zlib.h"` in the two source files - espmanger.h and espreadbiom.cpp (ignore the one after `#ifdef STATIC_ZLIB` as it is not used).

Why is this zlib thing so complicated:
1. vcpkg is a pain to install and get it to work correctly and requires include paths enviroment variables to be set up correctly.
2. zlib is not provided with VC++ sdk or standard libraries but it is used by the esp file format and the Creation Kit.
3. If zlib is statically linked to avoid this dependancy, the built .exe can cause antivirus generic detecton algorithms to false positive because decompression and compression code in an .exe looks suspicious when combined with file writes for something new that is not signed with a cert.
4. The code for this project assumes a vcpkg installed zlib so it uses `#include <zlib.h>` for an external library rather than `#include "zlib.h"` which would be one created as part of the project and can live with the source for the project. [see for more details](https://www.geeksforgeeks.org/difference-between-include-and-include-in-c-c-with-examples/)
   
### Features:
1. Allows creation of a star and planet based on an existing star or planet in order to create a new star system
2. Allows for additional planets and or moons to created in the star system
3. Creates the require locations to ensure navigation is possible in game
4. Extracts the required biom file and places in the data directory planetdata/biomemaps directory so the planet can be landed on (these must be included in any final ESP)
5. Allows ESM or ESP to be source of the data to be cloned from
6. Makes a back of the ESP when it is saved
7. Includes are star map to help decide what position the star should be placed in the in game map. Also includes basic planet map for a star system.
8. Checks how close to other stars to ensure not too close/overlapping
9. Performs name validation for new stars and planets
11. Auto names moons 

### Limitations:
1. Only very limited changes to the star or planet possible once cloned (to be expanded at some point)
2. The planet dialog includes an option to set the new planets position in the star system. This is currently not implemented. You can change the position in the creation kit if required.

### Use: 
1. Start the app. You will need to have a ESP to work on as your destination. This can be something created from a small change in the creation kit and then saving it.
2. File -> Select source master file (ESM) - Select the starfield.esm from your starfield data directory. 
3. File -> Select destination plugin file (ESP) - open a ESP being worked (make a copy!)
4. Star -> Create star - based on an existing star in the source
5. Planet -> Create planet - create a planet for the star from an existing planet in ESM.
   Note: A star system must have at least one planet to work in game. This step (and 6) will automatically extract the biom file for the planet from the correct starfield archive and rename it to the name of the new planet and place it in the data directory planetdata/biomemaps directory. 
6. Planet -> Create moon - optional
7. File -> Save to destination - will save to the loaded destination file. 
8. Check the ESP works in game.
9. Add other new stars, planets or moons as desired.
10. Do any refinement to the ESP in the creation kit.

### Notes:
1. You will need to include the .biom files extracted by step 5 for the new planet(s) in your final ESP. If these are excluded then it will not be possible to land on the planet.
2. A star contains a position on the in game world map. It's important not to have this same as an existing star, the app checks for this. However, this won't prevent some future ESP using the same position so try not to use a position where this is likely to happen, like center of map, or 0, 0, 0.
3. [xEdit](https://github.com/TES5Edit/TES5Edit/blob/bfabef91fe7f090c4ba81c865570b2e1ceb8f49d/whatsnew.md) is useful to validate the ESP after changes.

### License:
Licensed under: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0).

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.
