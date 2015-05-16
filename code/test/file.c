int main(void) {
    int fd, fd2;
    char c[100];
    Create("abc");
    fd = Open("abc");
    if (fd == -1) Exit(-1);
    Write("xxooxxooxx", 10, fd);
    Close(fd);
    fd = Open("abc");
    Read(c, 10, fd);
    Create("result");
    fd2 = Open("result");
    if (fd2 == -2) Exit(fd2);
    Write(c, 10, fd2);
    Write(c, 10, fd2);
    Close(fd);
    Close(fd2);
    Exit(fd);
}
