#include "Task.h"
#include "Environment.h"
#include "Utils.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <algorithm>

#include <json/json.h>
#include <pqxx/pqxx>

using namespace std;

void sendJsonResponse(Session *session, const Json::Value &jsonRoot);
int parseIdString(string idString);

void Annotation::run(Session *session, const string &argument)
{
    if (session->loglevel >= 3)
        (*session->logfile) << "Annotation handler reached" << std::endl;

    // Time this command
    if (session->loglevel >= 2)
        command_timer.start();

    size_t slashPosition = argument.find("/");
    string command = argument.substr(0, slashPosition);
    string args = argument.substr(slashPosition + 1, argument.size());
    if (slashPosition == string::npos || args == "")
    {
        throw annotation_error("No arguments were specified for annotation command!\n");
    }

    if (command == "getList")
    {
        getList(session, args);
    }
    else if (command == "load")
    {
        int id = parseIdString(args);
        load(session, id);
    }
    else if (command == "save")
    {
        vector<string> splitArgs = Utils::split(args, ",", 3);
        save(session, splitArgs[0], splitArgs[1], splitArgs[2]);
    }
    else if (command == "remove")
    {
        int id = parseIdString(args);
        remove(session, id);
    }

    if (session->loglevel >= 2)
    {
        *(session->logfile) << "Annotation :: Total command time " << command_timer.getTime() << " microseconds" << endl;
    }
}

int parseIdString(string idString)
{
    int id;
    try
    {
        id = stoi(idString);
    }
    catch (const exception &e)
    {
        throw annotation_error("Conversion of id " + idString + " failed!\n");
    }
    return id;
}

void Annotation::getList(Session *session, const string &tissuePath)
{
    if (session->loglevel >= 3)
        (*session->logfile) << "Annotation:: getList handler reached" << std::endl;

    string tissueAbsPath = Environment::getFileSystemPrefix() + tissuePath +
                           Environment::getFileSystemSuffix();
    ifstream tissueFile(tissueAbsPath);
    if (!tissueFile.good())
        throw annotation_error(tissuePath + " does not exist!\n");

    pqxx::result result = Utils::executeNonTransaction(session, "getList", tissuePath);

    Json::Value root;
    root["tissuePath"] = tissuePath;
    root["annotations"] = Json::arrayValue;

    for (auto const &row : result)
    {
        Json::Value annotation;
        annotation["id"] = stoi(row[0].c_str());
        annotation["name"] = row[1].c_str();
        root["annotations"].append(annotation);
    }

    sendJsonResponse(session, root);
}

void Annotation::load(Session *session, int annotationId)
{
    if (session->loglevel >= 3)
        (*session->logfile) << "Annotation:: load handler reached" << std::endl;

    Json::Value root;

    pqxx::result result = Utils::executeNonTransaction(session, "load", annotationId);
    for (auto const &row : result)
    {
        string path = row[0].c_str();
        Json::Value annotation;
        ifstream jsonFile(path);
        if (jsonFile.good())
        {
            jsonFile >> root;
        }
    }
    sendJsonResponse(session, root);
}

Json::Value parseJson(const string &jsonString);

void Annotation::save(Session *session, const string &tissuePath,
                      const string &jsonName, const string &jsonString)
{
    if (session->loglevel >= 3)
        (*session->logfile) << "Annotation:: save handler reached" << std::endl;
    Json::Value annotationRoot = parseJson(jsonString);

    pqxx::result getTissueIdResult = Utils::executeNonTransaction(
        session, "getTissueIdAndAbsPath", tissuePath);

    string tissueAbsPath;
    int tissueId;
    if (getTissueIdResult.empty())
    {
        tissueAbsPath = Environment::getFileSystemPrefix() + tissuePath +
                        Environment::getFileSystemSuffix();
        ifstream tissueFile(tissueAbsPath);
        if (!tissueFile.good())
        {
            throw annotation_error(tissuePath + " does not exist!\n");
        }

        pqxx::result insertTissueResult = Utils::executeTransaction(
            session, "insertTissue", tissuePath, tissueAbsPath);
        tissueId = insertTissueResult[0][0].as<int>();
    }
    else
    {
        tissueId = getTissueIdResult[0][0].as<int>();
        tissueAbsPath = getTissueIdResult[0][1].c_str();
    }

    string annotFilePrefix = Environment::getAnnotFolder();
    string tissueName = Utils::getFileName(tissuePath);
    string newJsonName = tissueName + "_" + to_string(tissueId) + "-" + jsonName + ".json";
    string absJsonPath = annotFilePrefix + newJsonName;

    ofstream outFile(absJsonPath);
    Json::StyledWriter styledWriter;
    outFile << styledWriter.write(annotationRoot);

    Utils::executeTransaction(
        session, "insertAnnotation", jsonName + ".json", absJsonPath, tissueId);

    Json::Value responseRoot;
    responseRoot["success"] = true;
    sendJsonResponse(session, responseRoot);
    return;
}

void Annotation::remove(Session *session, int annotationId)
{
    pqxx::result getAnnotationAbsPathResult = Utils::executeNonTransaction(
        session, "getAnnotationAbsPath", annotationId);
    if (getAnnotationAbsPathResult.empty())
    {
        throw annotation_error("Wrong annotation id!\n");
    }
    string annotationPath = getAnnotationAbsPathResult[0][0].c_str();
    if (std::remove(annotationPath.c_str()) != 0)
    {
        throw annotation_error("Error while deleting annotation " + annotationPath + "\n");
    }
    Utils::executeTransaction(
        session, "deleteAnnotation", annotationId);

    Json::Value responseRoot;
    responseRoot["success"] = true;
    sendJsonResponse(session, responseRoot);
}

Json::Value parseJson(const string &jsonString)
{
    Json::Value annotationRoot;
    Json::CharReaderBuilder builder;
    Json::CharReader *reader = builder.newCharReader();
    bool parsingSuccessful = reader->parse(jsonString.c_str(),
                                           jsonString.c_str() + jsonString.size(),
                                           &annotationRoot, nullptr);
    if (!parsingSuccessful)
    {
        throw annotation_error("Error while parsing json!\n");
    }
    return annotationRoot;
}

void sendJsonResponse(Session *session, const Json::Value &jsonRoot)
{
    string json = jsonRoot.toStyledString();
    stringstream header;
    header << session->response->createHTTPHeader(
        "json", "");
    if (session->out->putStr((const char *)header.str().c_str(), header.tellp()) == -1)
    {
        if (session->loglevel >= 1)
        {
            *(session->logfile) << "Annotation :: Error writing HTTP header" << endl;
        }
    }
    session->out->putStr((const char *)json.c_str(), json.size());
    session->response->setImageSent();
}