#include <iostream>
#include <unistd.h>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "../include/kptools.h"
#include <vector>
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <iterator>
#include <random>
#include <sstream>

using namespace gametools;

std::string randomNumber()
{
    static std::vector<int> v = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static std::random_device rd;
    static std::mt19937 generator(rd());
    std::shuffle(v.begin(), v.end(), generator);
    std::ostringstream oss;
    std::copy(v.begin(), v.begin() + 4, std::ostream_iterator<int>(oss, ""));
    return oss.str();
}

void client(std::string &playerName, std::string &sessionName, int state, int cnt)
{
    std::string apiSemName = sessionName + "api.semaphore";
    sem_t *apiSem = sem_open(apiSemName.c_str(), O_CREAT, accessPerm, 0);
    semSetvalue(apiSem, cnt);
    int apiState = 0;
    sem_getvalue(apiSem, &apiState);
    if (state > 1) {
        while (apiState != state) {
            sem_getvalue(apiSem, &apiState);
        }
    }
    sem_getvalue(apiSem, &apiState);
    sem_close(apiSem);
    std::string gameSemName = sessionName + "game.semaphore";
    sem_t *gameSem;
    if (state == 1) {
        sem_unlink(gameSemName.c_str());
        gameSem = sem_open(gameSemName.c_str(), O_CREAT, accessPerm, 0);
    } else {
        gameSem = sem_open(gameSemName.c_str(), O_CREAT, accessPerm, 0);
    }
    sem_getvalue(gameSem, &apiState);
    int firstIt = 1, cntOfBulls = 0, cntOfCows = 0, flag = 1;
    while (flag) {
        sem_getvalue(gameSem, &apiState);
        if (apiState % (cnt + 1) == state) {
            int gameFd = shm_open((sessionName + "game.back").c_str(), O_RDWR | O_CREAT, accessPerm);
            struct stat statBuf;
            fstat(gameFd, &statBuf);
            int sz = statBuf.st_size;
            ftruncate(gameFd, sz);
            char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, gameFd, 0);
            std::string strToJson = mapped;
            nlohmann::json request = nlohmann::json::parse(strToJson);
            if (request.contains("winner")) {
                std::cout << "Game over. Winner is " << request["winner"] << std::endl;
                sem_post(gameSem);
                flag = 0;
                break;
            }
            std::string ansField = playerName + "ans";
            std::string bullsField = playerName + "bulls";
            std::string cowsField = playerName + "cows";
            std::cout << "statistic of player " << playerName << ":" << std::endl;
            std::cout << "for answer: " << request[ansField] << std::endl;
            std::cout << "count of bulls: " << request[bullsField] << std::endl;
            std::cout << "count of cows: " << request[cowsField] << std::endl;
            std::string answer;
            std::cout << "Input number length of 4 with different digits: ";
            std::cin >> answer;
            if (answer.length() == 4) {
                std::set<char> s;
                for (int i = 0; i < 4; i++) {
                    s.insert(answer[i]);
                }
                if (s.size() != 4) {
                    std::cout << "\nWrong number. Try again" << std::endl;
                    continue;    
                }
            } else {
                std::cout << "\nWrong number. Try again" << std::endl;
                continue;
            }
            request[ansField] = answer;
            std::string strFromJson = request.dump();
            char *buffer = (char *) strFromJson.c_str();
            sz = strlen(buffer) + 1;
            ftruncate(gameFd, sz);
            mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, gameFd, 0);
            memset(mapped, '\0', sz);
            sprintf(mapped, "%s", buffer);
            munmap(mapped, sz);
            close(gameFd);
            sem_post(gameSem);
        }
    }
}

int createSession(std::string &playerName, std::string &sessionName, int cntOfPlayers)
{
    int state2 = 0;
    nlohmann::json createRequest;
    createRequest["type"] = "create";
    createRequest["name"] = playerName;
    createRequest["bulls"] = 0;
    createRequest["cows"] = 0;
    createRequest["ans"] = "0000";
    createRequest["sessionName"] = sessionName;
    createRequest["cntOfPlayers"] = cntOfPlayers;
    createRequest["hiddenNum"] = randomNumber();
    sem_t *mainSem = sem_open(mainSemName.c_str(), O_CREAT, accessPerm, 0);
    int state = 0;
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    int mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    std::string strFromJson = createRequest.dump();
    char *buffer = (char *) strFromJson.c_str();
    int sz = strlen(buffer) + 1;
    ftruncate(mainFd, sz);
    char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    memset(mapped, '\0', sz);
    sprintf(mapped, "%s", buffer);
    munmap(mapped, sz);
    close(mainFd);
    sem_wait(mainSem);
    sem_getvalue(mainSem, &state);
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    struct stat statBuf;
    fstat(mainFd, &statBuf);
    sz = statBuf.st_size;
    ftruncate(mainFd, sz);
    mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    nlohmann::json reply;
    std::string strToJson = mapped;
    reply = nlohmann::json::parse(strToJson);
    if (reply["check"] == "ok") {
        std::cout << "Session " << sessionName << " created" << std::endl; 
        state2 = reply["state"];
        sem_wait(mainSem);
        return 0;
    } else {
        std::cout << "Fail: name " << sessionName << " is already exists" << std::endl;
        sem_wait(mainSem);
        return 1;
    }
}

void joinSession(std::string &playerName, std::string &sessionName)
{
    nlohmann::json joinRequest;
    joinRequest["type"] = "join";
    joinRequest["name"] = playerName;
    joinRequest["bulls"] = 0;
    joinRequest["cows"] = 0;
    joinRequest["ans"] = "0000";
    joinRequest["sessionName"] = sessionName;
    sem_t *mainSem = sem_open(mainSemName.c_str(), O_CREAT, accessPerm, 0);
    int state = 0;
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    int mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    std::string strFromJson = joinRequest.dump();
    char *buffer = (char *) strFromJson.c_str();
    int sz = strlen(buffer) + 1;
    ftruncate(mainFd, sz);
    char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    memset(mapped, '\0', sz);
    sprintf(mapped, "%s", buffer);
    munmap(mapped, sz);
    close(mainFd);
    sem_wait(mainSem);
    sem_getvalue(mainSem, &state);
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    struct stat statBuf;
    fstat(mainFd, &statBuf);
    sz = statBuf.st_size;
    ftruncate(mainFd, sz);
    mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    nlohmann::json reply;
    std::string strToJson = mapped;
    reply = nlohmann::json::parse(strToJson);
    int state2 = 0, cnt = 0;
    if (reply["check"] == "ok") {
        std::cout << "Session " << sessionName << " joined" << std::endl; 
        state2 = reply["state"];
        cnt = reply["cnt"];
    } else {
        std::cout << "Fail: name " << sessionName << " is not exists" << std::endl;
    }
    sem_wait(mainSem);
    if (reply["check"] == "ok") {
        client(playerName, sessionName, state2, cnt);
    }
}

std::string findSession(std::string &playerName)
{
    nlohmann::json findRequest;
    findRequest["type"] = "find";
    findRequest["name"] = playerName;
    findRequest["bulls"] = 0;
    findRequest["cows"] = 0;
    findRequest["ans"] = "0000";
    sem_t *mainSem = sem_open(mainSemName.c_str(), O_CREAT, accessPerm, 0);
    int state = 0;
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    int mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    std::string strFromJson = findRequest.dump();
    char *buffer = (char *) strFromJson.c_str();
    int sz = strlen(buffer) + 1;
    ftruncate(mainFd, sz);
    char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    memset(mapped, '\0', sz);
    sprintf(mapped, "%s", buffer);
    munmap(mapped, sz);
    close(mainFd);
    sem_wait(mainSem);
    sem_getvalue(mainSem, &state);
    while (state != 1) {
        sem_getvalue(mainSem, &state);
    }
    mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
    struct stat statBuf;
    fstat(mainFd, &statBuf);
    sz = statBuf.st_size;
    ftruncate(mainFd, sz);
    mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
    nlohmann::json reply;
    std::string strToJson = mapped;
    reply = nlohmann::json::parse(strToJson);
    int state2 = 0, cnt = 0;
    std::string sessionName;
    if (reply["check"] == "ok") {
        std::string sessionName = reply["sessionName"];
        std::cout << "Session " << sessionName << " joined" << std::endl; 
        state2 = reply["state"];
        cnt = reply["cnt"];
    } else {
        std::cout << "Fail: session not found" << std::endl;
    }
    sem_wait(mainSem);
    if (reply["check"] == "ok") {
        return reply["sessionName"];
    } else {
        return "";
    }
}

int main(int argc, char const *argv[])
{
    std::cout << "Input your name: ";
    std::string playerName;
    std::cin >> playerName;
    std::cout << std::endl;
    std::vector<int> v;
    Player player;
    std::string command;
    std::cout << "Write:\n command [arg1] ... [argn]\n";
    std::cout << "\ncreate [name] [cntOfPlayers] to create new game session by name and max count of players\n";
    std::cout << "\njoin [name] to join exists game session by name\n";
    std::cout << "\nfind to find game session\n\n";
    int flag = 1;
    while (flag) {
        std::cin >> command;
        if (command == "create") {
            std::string name;
            int cntOfPlayers;
            std::cin >> name >> cntOfPlayers;
            if (cntOfPlayers < 2) {
                std::cout << "Error: count of players must be greater then 1\n";
            }
            int c = createSession(playerName, name, cntOfPlayers);
            if (c == 0) {
                joinSession(playerName, name);
            }
            flag = 0;
        } else if (command == "join") {
            std::string name;
            std::cin >> name;
            joinSession(playerName, name);
            flag = 0;
        } else if (command == "find") {
            std::string sessionName = findSession(playerName);
            if (sessionName != "") {
                joinSession(playerName, sessionName);
            }
            flag = 0;
        } else {
            std::cout << "Wrong command!\n"; 
            continue;
        }
    }
    return 0;
}
