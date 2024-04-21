#include <iostream>
#include <pthread.h>
using namespace std;

class Single {
    private:
    static Single * instance;
    Single() {}
    ~Single() {}

    public:
    static Single * getInstance();
};

Single * Single :: instance = new Single();
Single* Single :: getInstance() {
   return instance;
}

int main() {
    Single * p1 = Single :: getInstance();
    Single * p2 = Single :: getInstance();
    if (p1 == p2)
        cout << "same" << endl;
    
    return 0;
}