#include <Utils.h>
#include <algorithm>

using namespace std;

vector<string> Utils::split(const string &argument,
                            const string &delimiter,
                            int limit /*=-1*/)
{
    vector<string> strings;

    int counter = 1;
    auto start = 0U;
    auto end = argument.find(delimiter);
    while (end != string::npos)
    {
        if (counter == limit)
        {
            strings.push_back(argument.substr(start, argument.size() - 1));
            return strings;
        }
        strings.push_back(argument.substr(start, end - start));
        start = end + delimiter.length();
        end = argument.find(delimiter, start);
        counter++;
    }
    strings.push_back(argument.substr(start, end));
    return strings;
}

string Utils::getFileName(const string &filePath)
{
    string tissueName = filePath.substr(
        filePath.find_last_of('/') + 1, filePath.size() - 1);
    tissueName = tissueName.substr(0, tissueName.find_first_of('.'));
    return tissueName;
}

string Utils::fixFilePath(const string &filePath)
{
    string newPath = filePath;
    if (newPath.back() != '/')
    {
        newPath.push_back('/');
    }
    return newPath;
}

string Utils::trim(const string &s)
{
    return rtrim(ltrim(s));
}

string Utils::toLower(const string &s)
{
    string lower;
    transform(s.begin(), s.end(), lower.begin(),
              [](unsigned char c)
              { return tolower(c); });
    return lower;
}

// private:

const string WHITESPACE = " \n\r\t\f\v";

string Utils::ltrim(const string &s)
{
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == string::npos) ? "" : s.substr(start);
}

string Utils::rtrim(const string &s)
{
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == string::npos) ? "" : s.substr(0, end + 1);
}