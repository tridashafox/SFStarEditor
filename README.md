Original creation by tridasha.

Windows GUI tool that lets you create a new star system with a planet in an existing Starfield ESP pluging file.
C++ visual studio 2022 project. 

Note: Requires zlib1.dll which can be installed with vcpkg https://vcpkg.io/en/package/zlib if are compiling the source. 
I say this like it would simply work. but first you need visual studion installed, then you need vcpkg installed, and built, 
then you need the executable on your path, etc, etc. Note, vcpkg will get the package from https://github.com/madler/zlib pull and build that. 

In reality, zlib1.dll is flying around all over the place with many apps including a version of it. But it's not on windows by default.
You can just get a copy of it from \NifSkope directory if you have that installed.  Don't go to some random website to get a version of it!

Limitations:
1. Creates a new star and planet by duplicating an existing star or planet with some changes (name and position)
2. Does not allow more than one planet to be added currently (working on this)
3. Only very limited changes to the star or planet

To be be Licensed under: [CC BY-NC-SA 4.0](https://pages.github.com/](https://creativecommons.org/licenses/by-nc-sa/4.0/)).

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

If you want to contribute to this, let me know.
