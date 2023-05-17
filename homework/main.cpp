#include <iostream>
#include <thread>

#include "async.h"

void t1(std::size_t bulk)
{
    auto h = async::connect(bulk);
    async::receive(h, "1", 1);
    async::receive(h, "\n2\n3\n4\n5\n6\n{\na\n", 15);
    async::receive(h, "b\nc\nd\n}\n89\n", 11);
    async::disconnect(h);

}

void t2(std::size_t bulk)
{
    auto h2 = async::connect(bulk);
    async::receive(h2, "1\n", 2);
    async::disconnect(h2);

}

int main(int, char *[]) {
    std::size_t bulk = 5;

    std::thread th1(&t1, bulk);
    std::thread th2(&t2, bulk);

    th2.join();
    th1.join();
/*
    auto h = async::connect(bulk);
    auto h2 = async::connect(bulk);
    async::receive(h, "1", 1);
    async::receive(h2, "1\n", 2);
    async::receive(h, "\n2\n3\n4\n5\n6\n{\na\n", 15);
    async::receive(h, "b\nc\nd\n}\n89\n", 11);
    async::disconnect(h);
    async::disconnect(h2);
*/
    return 0;
}
