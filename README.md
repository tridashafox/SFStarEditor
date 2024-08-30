# Starfield star system editor for ESP moding
Original creation by tridasha.

**EXPERIMENTAL** Windows GUI tool that lets you create a new star system with planets in an existing Starfield ESP pluging file.
C++ Visual Studio 2022 project. 

Note: Requires zlib1.dll which can be installed with vcpkg https://vcpkg.io/en/package/zlib if compiling the source. 

### Releases:
[Release 0.1](https://github.com/tridashafox/SFStarEditor/releases/tag/Release_01).
IMPORTANT - This is an early release with limited testing. Beware if you use it with an ESP and then take that forward and there is found to be a data issue with what it has generated that might be hard to correct later.

### Features:
1. Allows creation of a star and planet based on an existing star or planet in order to create a new star system
2. Allows for additional planets to created in the star system
3. Creates the require locations to ensure navigation is possible in game
4. Extracts the required biom file and places in the data directory planetdata/biomemaps directory so the planet can be landed on (these must be included in any final ESP)
5. Allows ESM or ESP to be source of the data to be cloned from
6. Makes a back of the ESP when it is saved
7. Includes are star map to help decide what position the star should be placed in the 3d map.
8. Checks how close to other stars to ensure not too close/overlapping
9. Performs name a degree of name validation for new stars and planets
10. Repositions planets and moons in sequence to ensure positions are correct
11. Auto names moons 

### Limitations:
1. Only very limited changes to the star or planet possible once cloned (to be expanded at some point)

### Use: 
1. Start the app. You will need to have a ESP to work on as your destination. This can be something created from a small change in the creation kit and then saving it.
2. File -> Select source master file (ESM) - Select the starfield.esm from your starfield data directory. 
3. File -> Select destination plugin file (ESP) - open a ESP being worked (make a copy!)
4. Star -> Create star - based on an existing star in the source
5. Planet -> Create planet - create a planet for the star from an existing planet in ESM.
   Note: A star system must have at least one planet to work in game.
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
