#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

#define ASCII_CONST 100
#define DIRECTORY_LEN 1024
#define LOGIN_LEN 500

vector<pid_t> zombies;
int backgroundFlagLocation = -1;

vector<string> split(string s, char c)
{
  istringstream ss(s);
  string token;
  vector<string> tokens;
  while(getline(ss,token,c))
    tokens.push_back(token);
  return tokens;
}

string my_trim(string str)
{
    if(str[0] == ' ') // removes leading space on output file
        str = str.substr(1, string::npos);
    if(str[str.size()-1] == ' ') // removes trailing space on output file
        str = str.substr(0, str.size()-1);

    return str;
}

bool checkBackgroundProcess(string inputLine)
{
    vector<string> cmdargs = split(inputLine, ' ');
            
    for(int i = 0; i < cmdargs.size(); i++)
    {
        if(cmdargs[i] == "&")
            return true;
    }

    return false;
}

string checkQuotes(string inputline)
{
    bool singleQuote = false;
    bool doubleQuote = false;

    for(int i = 0; i < inputline.size(); i++)
    {
        if(inputline[i] == '\'')
            singleQuote = !singleQuote;
        else if(inputline[i] == '\"')
            doubleQuote = !doubleQuote;

        if(singleQuote || doubleQuote)
        {
            if(inputline[i] == '<' || inputline[i] == '>' || inputline[i] =='|' || inputline[i] == '&' || inputline[i] == ' ')
                inputline[i] += ASCII_CONST;
        }
    }

    return inputline;
}

string returnSymbolsInQuotes(string inputline)
{
    bool singleQuote = false;
    bool doubleQuote = false;

    for(int i = 0; i < inputline.size(); i++)
    {
        if(inputline[i] == '\'')
            singleQuote = !singleQuote;
        else if(inputline[i] == '\"')
            doubleQuote = !doubleQuote;

        if(singleQuote || doubleQuote)
        {
            if(inputline[i] == char('<' + ASCII_CONST) || inputline[i] == char('>' + ASCII_CONST)
               || inputline[i] == char('|' + ASCII_CONST) || inputline[i] == char('&' + ASCII_CONST)
               || inputline[i] == char(' ' + ASCII_CONST))
                inputline[i] -= ASCII_CONST;
        }
    }

    string output;
    output.reserve(inputline.size()); // optional, avoids buffer reallocations in the loop
    for(unsigned int i = 0; i < inputline.size(); ++i)
        if(inputline[i] != '\'' && inputline[i] != '\"')
            output += inputline[i];

    return output;
}


void execCommand(string inputLine)
{
    int fdInput;
    int fdOutput;
    if(inputLine.find(">") != string::npos) // redirect output
    {
        vector<string> outputRedirect = split(inputLine, '>'); // ex: ps aux > a --> ["ps aux " , " a"]
        inputLine = outputRedirect[0];
        string outputFile = outputRedirect[1];
        outputFile = my_trim(outputFile);
        inputLine = my_trim(inputLine);
        fdOutput = open(outputFile.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0700);
        dup2(fdOutput, 1); // overwrite STDOUT
    }
    if(inputLine.find("<") != string::npos) // redirect input
    {
        vector<string> inputRedirect = split(inputLine, '<'); // ex: grep /init < a --> ["grep /init " , " a"]
        inputLine = inputRedirect[0];
        string inputFile = inputRedirect[1];
        inputFile = my_trim(inputFile);
        inputLine = my_trim(inputLine);
        fdInput = open(inputFile.c_str(), O_RDONLY, 0700);
        dup2(fdInput, 0); // overwrite STDIN
    }
    
    vector<string> cmdargs = split(inputLine, ' ');
    for(int i = 0; i < cmdargs.size(); i++)
        cmdargs[i] = returnSymbolsInQuotes(cmdargs[i]);
    
    for(int i = 0; i < cmdargs.size(); i++)
    {
        if(cmdargs[i] == "&")
            backgroundFlagLocation = i;
    }
    if(backgroundFlagLocation != -1)
    {
        cmdargs.erase(cmdargs.begin()+backgroundFlagLocation);
    }
        
    char* args[cmdargs.size()+1];
    for(int i = 0; i < cmdargs.size(); i++)
    {
        args[i] = (char*) cmdargs[i].c_str();
    }
    args[cmdargs.size()] = NULL;
    execvp(args[0],args);
}


void execPipedCommands(vector<string> pipedCommands)
{
    for(int i = 0; i < pipedCommands.size(); i++)
    {
        int fd[2];
        pipe(fd);
        int cid = fork();

        if(!cid) // child
        {
            // redirect output (except for last child)
            if(i < pipedCommands.size() - 1)
                dup2(fd[1], 1);
            // execute command
            execCommand(pipedCommands[i]);
        }   
        else // parent
        {
            // wait
            waitpid(cid, 0, 0);
            // redirect input
            dup2(fd[0], 0);
            // closed so next level does not wait
            close(fd[1]);
        }

    }

}


int main(int argc, char *argv[])
{
    char* loginBuf = new char[LOGIN_LEN];
    getlogin_r(loginBuf,sizeof(loginBuf));
    char currDirBuf[DIRECTORY_LEN];
    char prevDir[DIRECTORY_LEN];
    int pid = -1;
    
    while(true)
    {
        getcwd(currDirBuf, sizeof(currDirBuf));

        // handles unsupported commands by exiting child process
        if(pid == 0)
            exit(0);

        backgroundFlagLocation = -1; // reset background location

        cout << "MyShell@" << loginBuf << ":" << currDirBuf << "$ ";
        string inputLine;
        getline(cin, inputLine);

        // reap zombies
        for(vector<pid_t>::iterator i = zombies.end(); i != zombies.begin();)
        {
            if(waitpid(*(--i), 0, WNOHANG))
                zombies.erase(i);   
        }

        if(inputLine == string("exit"))
        {
            cout << "Bye!! End of shell" << endl;
            delete loginBuf;
            break;
        }
        if(inputLine.find("cd") == 0) // if command starts with cd
        {
            vector<string> args = split(inputLine, ' ');
            // move to prev dir
            if(args[1] == "-")
            {
                // no prev directory to move to
                if(prevDir[0] == '\0')
                    continue;
                else
                    chdir(prevDir);
            }
            // regular cd call
            else
            {
                memcpy(&prevDir, &currDirBuf, DIRECTORY_LEN);
                chdir(args[1].c_str());
            }
            continue;
        }

        inputLine = checkQuotes(inputLine);
        bool backgroundProcess = checkBackgroundProcess(inputLine);
        pid = fork();

        if(pid == 0) // child
        {
            // handles pipes
            if(inputLine.find("|") != string::npos)
            {
                vector<string> pipedCommands = split(inputLine, '|');
                for(int i = 0; i < pipedCommands.size(); i++)
                    pipedCommands[i] = my_trim(pipedCommands[i]);

                execPipedCommands(pipedCommands);
            }
            // no pipes
            else
                execCommand(inputLine);
        }
        else // parent
        {
            // does not wait if a background process
            if(!backgroundProcess)
                waitpid(pid,0,0);
            else // pushes child pid if it is a background process
                zombies.push_back(pid);
        }
    }
}