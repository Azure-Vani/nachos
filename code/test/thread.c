void f() {
    int i; char c2[10];
    c2[1] = '\n';
    for (i = 'a'; i <= 'z'; i++) {
        c2[0] = i;
        Write(c2, 2, 1);
        Yield();
    }
    Exit(1);
}

int main() {
    int j; char c1[10];
    int ret = Exec("file");
    Join(ret);
    c1[1] = '\n';
    for (j = 'a'; j <= 'z'; j++) {
        c1[0] = j;
        Write(c1, 2, 1);
    }
    Exit(ret);
}

