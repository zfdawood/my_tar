compile using `gcc inodemap.c mytar.c -o mytar` 

to turn a directory into a tarfile, use `./mytar -c -f <TARFILE NAME>.tar ./path/to/directory`

to get a summary of all files in the tarfile use `./mytar -t -f <TARFILE NAME>.tar`

to extract the tarfile, use `./mytar -x -f <TARFILE NAME>.tar` (the extracted folder cannot have the same name as a folder already in the directory).
