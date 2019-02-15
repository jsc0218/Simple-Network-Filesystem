#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using namespace std;

int main() {
    string file = "/home/cz3liu/courses/cs798/project2/Simple-Network-Filesystem/tmp/testfile";

    int fd = open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    for (int i = 0; i < 1000; i++) {
        cout << i << endl;
        string numStr =  to_string(i) + "\n";
        write(fd, numStr.c_str(), numStr.length());
        sleep(1);
    }

    close(fd);

    cout << "fin" << endl;
    return 0;
}
