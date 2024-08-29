Original creation by tridasha.

**EXPERIMENTAL** Windows GUI tool that lets you create a new star system with planets in an existing Starfield ESP pluging file.
C++ Visual Studio 2022 project. 

Note: Requires zlib1.dll which can be installed with vcpkg https://vcpkg.io/en/package/zlib if compiling the source. 

Releases:
IMPORTANT - This is an earily release with limited testing. Beware if you use it with an ESP and then take that forward and there is found to be a data issue with what it has generated that might be hard to correct later.
[Release 0.1](https://github.com/tridashafox/SFStarEditor/releases/tag/Release_01).

Features:
1. Allows creation of a star and planet based on an existing star or planet in order to create a new star system
2. Allows for additional planets to created in the star system
3. Creates the require locations to ensure navigation is posisble in game
4. Extracts the required biom file and places in the data directory planetdata/biomemaps directory so the planet can be landed on (these must be included in any final ESP)
5. Allows ESM or ESP to be source of the data to be cloned from
6. Makes a back of of the ESP when it is saved
7. Includes are star map to help decide what position the star should be placed in the 3d map.
8. Checks how close to other stars to ensure not too close/overlapping
9. Performs name a degree of name validation for new stars and planets
10. Repositions planets and moons in sequence to ensure positions are correct
11. Auto names moons 

Limitations:
1. Only very limited changes to the star or planet possible once cloned (to be expanded at some point)
2. All stars and planets are clones of an existing planet

Use: 
1. Start the app.
2. File -> Select source master file (ESM) - Select the starfield.esm file.
3. File -> Select destination plugin file (ESP) - open a ESP being worked (make a copy!)
4. Star -> Create star - based on an existing star in the source
5. Planet -> Create planet - create a planet for the star from an existing planet in ESM.
   Note: A star system must have at least one planet to work in game.
6. Planet -> Create moon - optional
7. File -> Save to destination - will save to the loaded destination file. 
8. Check the ESP works in game.
9. Add other new planets to the star system as desired.
10. Do any refinement to the ESP in the creation kit.

Notes:
1. You will need to include the .biom files extracted by step 5 for the new planet(s) in your final ESP. If these are excluded then it will not be possible to land on the planet.
2. The star contains the position on the in game world map. It's important not to have this same as an existing star, the app checks for this. However, this won't prevent some future ESP using the same position so try not to use a position where this is likely to happen, like center of map, or 0, 0, 0.
3. xEdit is useful to validate the ESP after changes.

Licensed under: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0).

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.

If you want to contribute to this, let me know.
