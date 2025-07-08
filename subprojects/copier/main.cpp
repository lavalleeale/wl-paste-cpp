#include "ClipboardCopier.h"
#include <iostream>

int main(int argc, char *argv[])
{
    std::string command;
    for (int i = 1; i < argc; ++i)
    {
        command += argv[i];
        if (i < argc - 1)
        {
            command += " ";
        }
    }
    ClipboardCopier copier(command);
    return copier.run();
}
