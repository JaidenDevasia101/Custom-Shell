#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>
#include <cstdlib>

using namespace std;

const char ERROR_MESSAGE[] = "An error has occurred\n";
void printError() {
    write(STDERR_FILENO, ERROR_MESSAGE, strlen(ERROR_MESSAGE));
}

vector<string> breakUpParts(const string& input, const string& delimiter = " ") {
    vector<string> Parts;
    size_t start = 0, end = 0;

    while ((end = input.find(delimiter, start)) != string::npos) {
        if (end != start) {
            Parts.push_back(input.substr(start, end - start));
        }
        start = end + delimiter.length();
    }

    if (start < input.size()) {
        Parts.push_back(input.substr(start));
    }
    return Parts;
}

string createPath(const string& command) {
    const char* path = getenv("PATH");

    if (!path) {
        return "";
    }

    vector<string> directories = breakUpParts(path, ":");
    for (const string& dir : directories) {
        string createdPath = dir + "/" + command;
        if (access(createdPath.c_str(), X_OK) == 0) {
            return createdPath;
        }
    }
    return "";
}

void execCommand(const vector<string>& args, const string& outputFile) {
    pid_t pid = fork();

    if (pid < 0) {
        printError();
        return;
    }

    if (pid == 0) {
        if (!outputFile.empty()) {
            int fd = open(outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
            if (fd < 0) {
                printError();
                exit(1);
            }

            if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
                printError();
                close(fd);
                exit(1);
            }
            close(fd);
        }

        vector<char*> c_args;
        string createdPath = createPath(args[0]);

        if (createdPath.empty()) {
            printError();
            exit(1);
        }

        for (const auto& arg : args) {
            c_args.push_back(const_cast<char*>(arg.c_str()));
        }
        c_args.push_back(nullptr);
        execv(createdPath.c_str(), c_args.data());
        printError();
        exit(1);

    } else {
        waitpid(pid, nullptr, 0);
    }
}

void handleIndividualCommand(string command, bool pathSet) {
    command = command.substr(0, command.find_last_not_of(" \t\n\r") + 1);

    size_t redirect = command.find('>');

    if (redirect != string::npos) {
        if (redirect > 0 && command[redirect - 1] != ' ') {
            command.insert(redirect, " ");
            redirect++;
        }

        if (redirect + 1 < command.size() && command[redirect + 1] != ' ') {
            command.insert(redirect + 1, " ");
        }
    }

    auto parts = breakUpParts(command);
    if (parts.empty()) return;
    string outputFile;
    bool invalidRedirection = false;

    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == ">") {
            if (i == 0 || i + 1 >= parts.size() || parts[i + 1].empty()) {
                invalidRedirection = true;
                break;
            }

            outputFile = parts[i + 1];

            if (i + 2 < parts.size()) {
                invalidRedirection = true;
                break;
            }
            parts.resize(i);
            break;
        }
    }

    if (invalidRedirection) {
        printError();
        return;
    }

    if (parts.empty()) {
        printError();
        return;
    }

    if (!pathSet && parts[0] != "exit" && parts[0] != "cd" && parts[0] != "path") {
        printError();
        return;
    }
    execCommand(parts, outputFile);
}

void handleLineCommand(const string& input, bool pathSet) {
    vector<string> commands = breakUpParts(input, "&");
    vector<pid_t> pids;

    for (const auto& command : commands) {
        pid_t pid = fork();
        if (pid < 0) {
            printError();
            return;
        } else if (pid == 0) {
            handleIndividualCommand(command, pathSet);
            exit(0);
        } else {
            pids.push_back(pid);
        }
    }

    for (pid_t pid : pids) {
        waitpid(pid, nullptr, 0);
    }
}

int main(int argc, char* argv[]) {
    if (argc > 2) {
        printError();
        exit(1);
    }
    setenv("PATH", "/bin:/usr/bin", 1);
    bool pathSet = true;
    string input;
    istream* inputStream = &cin;
    ifstream file;

    if (argc == 2) {
        file.open(argv[1]);
        if (!file.is_open()) {
            printError();
            exit(1);
        }
        inputStream = &file;
    }

    while (true) {
        if (argc == 1) {
            cout << "CustomShell> ";
        }

        if (!getline(*inputStream, input)) {
            break;
        }

        input = input.substr(0, input.find_last_not_of(" \t\n\r") + 1);
        if (input.empty()) {
            continue;
        }
        auto parts = breakUpParts(input);

        if (parts[0] == "exit") {
            if (parts.size() > 1) {
                printError();
            } else {
                exit(0);
            }

        } else if (parts[0] == "cd") {
            if (parts.size() != 2) {
                printError();
            } else if (chdir(parts[1].c_str()) != 0) {
                printError();
            }

        } else if (parts[0] == "path") {
            string newPath;
            for (size_t i = 1; i < parts.size(); ++i) {
                if (access(parts[i].c_str(), F_OK) == 0) {
                    newPath += parts[i];
                    if (i < parts.size() - 1) {
                        newPath += ":";
                    }
                }
            }

            if (!newPath.empty()) {
                newPath += ":/bin:/usr/bin";
                setenv("PATH", newPath.c_str(), 1);
                pathSet = true;
            } else {
                unsetenv("PATH");
                pathSet = false;
            }
        } else {
            handleLineCommand(input, pathSet);
        }
    }
    return 0;
}
