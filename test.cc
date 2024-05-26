#include <iostream>

template <typename T>
void func(T x) {
    static int a = 1;
    std::cout << a++ << std::endl;
}

int main(void) {
    func(1);
    func(2.0);
    func(2);
}