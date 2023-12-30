#include <iostream>
#include <unistd.h>
#include <thread>
#include <sys/mman.h>
#include <sys/stat.h>
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

std::map<std::string, Session> sessions;

int main(int argc, char const *argv[])
{
    sem_unlink(mainSemName.c_str());
    sem_t *mainSem = sem_open(mainSemName.c_str(), O_CREAT, accessPerm, 0);
    int state = 0;
    semSetvalue(mainSem, 1);
    sem_getvalue(mainSem, &state);
    while (1) {
        sem_getvalue(mainSem, &state);
        if (state == 0) {
            int mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
            struct stat statBuf;
            fstat(mainFd, &statBuf);
            int sz = statBuf.st_size;
            ftruncate(mainFd, sz);
            char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
            nlohmann::json createReply;
            std::string strToJson = mapped;
            munmap(mapped, sz);
            close(mainFd);
            createReply = nlohmann::json::parse(strToJson);
            nlohmann::json request;
            if (createReply.contains("type")) {
                std::string joinSemName;
                sem_t *joinSem;
                Player player;
                player.ans = createReply["ans"];
                player.bulls = createReply["bulls"];
                player.cows = createReply["cows"];
                player.name = createReply["name"];
                if (createReply["type"] == "create") {
                    joinSemName = createReply["sessionName"];
                    joinSemName += ".semaphore";
                    sem_unlink(joinSemName.c_str());
                    joinSem = sem_open(joinSemName.c_str(), O_CREAT, accessPerm, 0);
                    semSetvalue(joinSem, 0);
                    if (sessions.find(createReply["sessionName"]) == sessions.cend()) {
                        Session session;
                        session.sessionName = createReply["sessionName"];
                        session.cntOfPlayers = createReply["cntOfPlayers"];
                        session.hiddenNum = createReply["hiddenNum"];
                        session.playerList.push_back(player);
                        sessions.insert({session.sessionName, session});
                        for (auto i: sessions) {
                            std::cout << i.second << std::endl;
                        }
                        request["check"] = "ok";
                        request["state"] = 0;
                        pid_t serverPid = fork();
                        if (serverPid == 0) {
                            sem_close(mainSem);
                            execl("./server", "./server", strToJson.c_str(), NULL);
                            return 0;
                        }                        
                    } else {
                        request["check"] = "error";
                    }
                } else if (createReply["type"] == "join") {
                    joinSemName = createReply["sessionName"];
                    joinSemName += ".semaphore";
                    if (sessions.find(createReply["sessionName"]) != sessions.cend()) {
                        if (sessions[createReply["sessionName"]].cntOfPlayers <= sessions[createReply["sessionName"]].playerList.size()) {
                            request["check"] = "error";    
                        }
                        player.ans = createReply["ans"];
                        player.bulls = createReply["bulls"];
                        player.cows = createReply["cows"];
                        player.name = createReply["name"];
                        sessions[createReply["sessionName"]].playerList.push_back(player);
                        
                        std::string joinFdName = createReply["sessionName"];
                        int joinFd = shm_open(joinFdName.c_str(), O_RDWR | O_CREAT, accessPerm);
                        std::string strFromJson = createReply.dump();
                        char *buffer = (char *) strFromJson.c_str();
                        int sz = strlen(buffer) + 1;
                        ftruncate(joinFd, sz);
                        char *mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, joinFd, 0);
                        memset(mapped, '\0', sz);
                        sprintf(mapped, "%s", buffer);
                        munmap(mapped, sz);
                        close(joinFd);
                        sem_post(joinSem);
                        request["check"] = "ok";
                        request["state"] = sessions[createReply["sessionName"]].playerList.size() - 1;
                        request["cnt"] = sessions[createReply["sessionName"]].cntOfPlayers;
                    } else {
                        request["check"] = "error";
                    }
                } else if (createReply["type"] == "find") {
                    for (auto i: sessions) {
                        if (i.second.playerList.size() <= i.second.cntOfPlayers) {
                            request["sessionName"] = i.second.sessionName;
                            createReply["sessionName"] = i.second.sessionName;
                        }
                    }
                    if (!(request.contains("sessionName"))) {
                        request["check"] = "error";
                    }
                    if (!request.contains("check") || request["check"] != "error") {
                        player.ans = createReply["ans"];
                        player.bulls = createReply["bulls"];
                        player.cows = createReply["cows"];
                        player.name = createReply["name"];
                        request["check"] = "ok";
                        request["state"] = sessions[createReply["sessionName"]].playerList.size() - 1;
                        request["cnt"] = sessions[createReply["sessionName"]].cntOfPlayers;
                    }
                }
            } else {
                sem_post(mainSem);
                continue;
            }
            std::string strFromJson = request.dump();
            char *buffer = (char *) strFromJson.c_str();
            sz = strlen(buffer) + 1;
            mainFd = shm_open(mainFileName.c_str(), O_RDWR | O_CREAT, accessPerm);
            ftruncate(mainFd, sz);
            mapped = (char *) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, mainFd, 0);
            memset(mapped, '\0', sz);
            sprintf(mapped, "%s", buffer);
            munmap(mapped, sz);
            close(mainFd);
            sem_post(mainSem);
        }
    }
    return 0;
}
