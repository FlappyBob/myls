A simple Hackey ls function 

ls: List files
Usage: ls [OPTION]... [FILE]...
-a      list all files, including files starting with . and the pseudo-files . and ... 
-l      use a long listing format 
-n      suppresses the listing of files and instead produces a count of the number of files that would be printed if they were listed.
-R      recursively lists files in subdirectories
-h      prints “human readable”. With -l and -s, print sizes like 1K 234M 2G etc.
--hack  invoke the system-supplied ls
--help  display this help and exit