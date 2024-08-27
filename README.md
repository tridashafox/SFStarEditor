Original creation by tridasha.

**EXPERIMENTAL** Windows GUI tool that lets you create a new star system with planets in an existing Starfield ESP pluging file.
C++ visual studio 2022 project. 

Note: Requires zlib1.dll which can be installed with vcpkg https://vcpkg.io/en/package/zlib if are compiling the source. 
I say this like it would simply work. but first you need visual studio installed, then you need vcpkg installed, and built, 
then you need the vcpkg executable on your path, etc, etc. Note, vcpkg will get the package from https://github.com/madler/zlib and build that. 

In reality, zlib1.dll is flying around all over the place with many apps including a version of it. But it's not on windows by default.
You can just get a copy of it from the NifSkope directory if you have that installed. Don't go to some random website to get a version of it!

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
10. Repositions planets in sequence to ensure planet positions are correct

Limitations:
1. Does not support moons at the moment
2. Only very limited changes to the star or planet possible once cloned (to be expanded at some point)
3. All stars and planets are clones of an existing planet

Use: 
1. Start the app.
2. Select a source - open ESM and select the starfield.esm file.
3. Select a destination - open a ESP being worked (make a copy!)
4. Create a star based on an existing star in the source
5. Create a planet for the star (a star must have at least one planet to work in game)
6. Save, check ESP in game
7. add other new planets to the star.
8. Do any refinement in creation kit
9. Validate data in ESP using xEdit for starfield.

To be be Licensed under: [CC BY-NC-SA 4.0](https://pages.github.com/](https://creativecommons.org/licenses/by-nc-sa/4.0/)).

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the author be held liable for any damages
  arising from the use of this software.

If you want to contribute to this, let me know.
