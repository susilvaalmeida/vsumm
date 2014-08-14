#include "FileOperations.h"

#include <iostream>
using namespace std;

int FileOperations::createDir(string dirName)
{
	DIR* dir = opendir(dirName.c_str());
	if(dir == NULL)
	{
		string str = "mkdir " + dirName;
		system(str.c_str());
		return 1;
	}
	return 0;
}

int FileOperations::deleteDir(string dirName)
{
	DIR* dir = opendir(dirName.c_str());
	if(dir != NULL)
	{
		string str = "rmdir /S /Q " + dirName;
		system(str.c_str());
		return 1;
	}
	return 0;
}

vector<string> FileOperations::listFiles(string dirName, string type)
{
	std::vector<string> files;
	string searchkey = dirName + "*" + type;
	WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(searchkey.c_str(),&fd);
 
    if(h == INVALID_HANDLE_VALUE)
    {
        return files;
    }
 
    while(1)
    {
		if(fd.cFileName != "")
	        files.push_back(dirName + fd.cFileName);
 
        if(FindNextFile(h, &fd) == FALSE)
            break;
    }
	/*FILE *in;
	char buff[512];

	std::vector<string> files;

	string str = "find " + dirName + " -maxdepth 1 -name \"*" + type + "\" ";
	if(!(in = _popen(str.c_str(), "r")))
	{
		return files;
	}

	while(fgets(buff, sizeof(buff), in) != NULL)
	{
		string str(buff);
		vector<string> vstr = split(str, '\n');
		files.push_back(vstr[0]);
	}*/
	return files;
}

int FileOperations::createFile(string fileNameComplete, string content)
{
	ofstream file;
	file.open(fileNameComplete.c_str(),std::ofstream::out | std::ofstream::app);
	file << content;
	file.close();

	return 1;
}

int FileOperations::copyFile(string fileNameComplete, string newDir)
{
	string str = "copy " + fileNameComplete + " " + newDir;
	system(str.c_str());

	return 1;
}

int FileOperations::deleteFile(string fileNameComplete)
{
	string str = "del " + fileNameComplete;
	system(str.c_str());
	return 1;
}

string FileOperations::getSimpleName(string fileNameComplete)
{
	vector<string> splitBarra = split(fileNameComplete, '\\');
	vector<string> splitPonto = split(splitBarra[splitBarra.size()-1], '.');

	return splitPonto[0];
}

vector<string> FileOperations::split(const string &text, char sep)
{
	vector<string> tokens;
	int start = 0, end = 0;
	while ((end = text.find(sep, start)) != string::npos)
	{
		tokens.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	tokens.push_back(text.substr(start));
	return tokens;
}

