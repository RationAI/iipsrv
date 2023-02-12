#ifndef _UTILS_H
#define _UTILS_H

#include <string>
#include <vector>
#include <Task.h>

class Utils
{
public:
    /**
     * @brief Splits the string on the specified delimiter. 
     * 
     * @param argument string to split
     * @param delimiter delimiter
     * @param limit maximum number of strings
     * @return vector of split strings
     */
    static std::vector<std::string> split(const std::string &argument,
                                                 const std::string &delimiter,
                                                 int limit = -1);

    /**
     * @brief Gets file name without directory and suffix.
     *
     * @param filePath file path
     * @return file name
     */
    static std::string getFileName(const std::string &filePath);

    /**
     * @brief Adds '/' to the end of file path if needed.
     *
     * @param filePath file path
     * @return fixed file path
     */
    static std::string fixFilePath(const std::string &filePath);

    /**
     * @brief Removes whitespaces from the beginning and the end of the string.
     * 
     * @param s string to trim
     * @return std::string trimmed string
     */
    static std::string trim(const std::string &s);

    /**
     * @brief Converts string to lowercase.
     * 
     * @param s string to convert
     * @return converted string
     */
    static std::string toLower(const std::string &s);

private:
    static std::string ltrim(const std::string &s);
    static std::string rtrim(const std::string &s);
};

#endif
