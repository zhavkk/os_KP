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

using namespace gametools;

Session session;
std::map<char, int> hidV;

std::pair<int, int> ggame(std::string ans)
{
    int cntOfBulls = 0, cntOfCows = 0, ind = 0;
    for (int i = 0; i < ans.size(); i++) {
        if (hidV.find(ans[i]) != hidV.cend()) {
            if (hidV[ans[i]] == i) {
                cntOfBulls++;
            } else {
                cntOfCows++;
            }
        }
    }
    return std::make_pair(cntOfBulls, cntOfCows);
}

void server(std::string &sessionName)
{
    std::string tmp = session.hiddenNum;
    for (int i = 0; i < tmp.size(); i++) {
        hidV.insert({tmp[i], i});
    }
    std::string gameSemName = sessionName + "game.semaphore";
    sem_t *gameSem = sem_open(gameSemName.c_str(), O_CREAT, accessPerm, 0);
    semSetvalue(gameSem, 0);
    int state = 0, firstIt = 1;
    while (1) {
        sem_getvalue(gameSem, &state);
        if (state % (session.cntOfPlayers + 1) == 0 && firstIt == 0) {
            int gameFd = shm_open((sessionName + "game.back").c_str(), O_RDWR | O_CREAT, accessPerm);
            struct stat statBuf;
            fstat(gameFd, &statBuf);
            int sz = statBuf.st_size;
            ftruncate(gameFd, sz);
            char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, gameFd, 0);
            std::string strToJson = mapped;
            nlohmann::json request = nlohmann::json::parse(strToJson);
            for (auto i: session.playerList) {
                std::string ansField = i.name + "ans";
                std::string bullsField = i.name + "bulls";
                std::string cowsField = i.name + "cows";
                i.ans = request[ansField];
                std::pair<int, int> result = ggame(i.ans);
                i.bulls = result.first;
                i.cows = result.second;
                request[ansField] = i.ans;
                request[bullsField] = i.bulls;
                request[cowsField] = i.cows;
                if (i.bulls == 4) {
                    request["winner"] = i.name;
                }
            }
            std::string strFromJson = request.dump();
            char *buffer = (char *) strFromJson.c_str();
            sz = strlen(buffer) + 1;
            ftruncate(gameFd, sz);
            mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, gameFd, 0);
            memset(mapped, '\0', sz);
            sprintf(mapped, "%s", buffer);
            munmap(mapped, sz);
            close(gameFd);
            std::cout << session << std::endl;
            sem_post(gameSem);
        } else if (state % (session.cntOfPlayers + 1) == 0 && firstIt == 1) {
            nlohmann::json request;
            for (auto i: session.playerList) {
                std::string ansField = i.name + "ans";
                std::string bullsField = i.name + "bulls";
                std::string cowsField = i.name + "cows";
                request[ansField] = i.ans;
                request[bullsField] = i.bulls;
                request[cowsField] = i.cows;
            }
            std::string strFromJson = request.dump();
            int gameFd = shm_open((sessionName + "game.back").c_str(), O_RDWR | O_CREAT, accessPerm);
            char *buffer = (char *) strFromJson.c_str();
            int sz = strlen(buffer) + 1;
            ftruncate(gameFd, sz);
            char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, gameFd, 0);
            memset(mapped, '\0', sz);
            sprintf(mapped, "%s", buffer);
            munmap(mapped, sz);
            close(gameFd);
            firstIt = 0;
            std::cout << session << std::endl;
            sem_post(gameSem);
        }
    }
}

void waitAlllPlayers(std::string &sessionName)
{
    session.curPlayerIndex = 0;
    std::string joinSemName = (sessionName + ".semaphore");
    
    sem_t *joinSem = sem_open(joinSemName.c_str(), O_CREAT, accessPerm, 0);
    int state = 0;
    
    sem_getvalue(joinSem, &state);
    std::string joinFdName;
    while (state < session.cntOfPlayers) {
        sem_getvalue(joinSem, &state);
        if (state > session.curPlayerIndex) {
            joinFdName = sessionName.c_str();
            int joinFd = shm_open(joinFdName.c_str(), O_RDWR | O_CREAT, accessPerm);
            struct stat statBuf;
            fstat(joinFd, &statBuf);
            int sz = statBuf.st_size;
            ftruncate(joinFd, sz);
            char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, joinFd, 0);
            std::string strToJson = mapped;
            nlohmann::json joinReply;
            munmap(mapped, sz);
            close(joinFd);
            joinReply = nlohmann::json::parse(strToJson);
            Player playerN;
            playerN.name = joinReply["name"];
            playerN.ans = joinReply["ans"];
            playerN.bulls = joinReply["bulls"];
            playerN.cows = joinReply["cows"];
            session.playerList.push_back(playerN);
            session.curPlayerIndex = state;
        }
    }
}

int main(int argc, char const *argv[])
{
    std::string strToJson = argv[1];
    nlohmann::json reply;
    reply = nlohmann::json::parse(strToJson);
    session.sessionName = reply["sessionName"];
    session.cntOfPlayers = reply["cntOfPlayers"];
    session.hiddenNum = reply["hiddenNum"];
    session.curPlayerIndex = 0;
    
    std::string apiSemName = session.sessionName + "api.semaphore";
    sem_unlink(apiSemName.c_str());
    sem_t *apiSem = sem_open(apiSemName.c_str(), O_CREAT, accessPerm, 0);
    semSetvalue(apiSem, session.cntOfPlayers);
    int f = 0;
    sem_getvalue(apiSem, &f);
    
    waitAlllPlayers(session.sessionName);

    while (f > 0) {
        sem_wait(apiSem);
        sem_getvalue(apiSem, &f);
        std::cout << f << std::endl;
        sleep(1);
    }
    sem_getvalue(apiSem, &f);
    server(session.sessionName);
    return 0;
}
