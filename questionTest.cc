// S.length = 10,0000, M >= 2 workers, 
// For each users, read 3 elements each time, repeats 10000 times
/*
Given an integer array S of length N (N = 100,000), there are M (M >= 2) workers concurrently accessing and updating S. Each worker repeats the following operation 10000 times:
Generate random numbers i and j, 0 <= i, j< 100000. Update S such that S(j)=S(i)+ S(i+1)+ S(i+2). lf i+ 1 or i +2 is out of bounds, then use (i+1) % N or (i+2) %N.
Hint
(a) Please consider concurrent protection, i.e.. reading S(i),S(i+1), S(i+2) and updating S(i) are atomic operations.
*Refer to the two-phase locking algorithm
(b) Pay attention to the lock granularity. Each worker only reads 3 elements at a time and writes 1 element. There are a total of 100000 elements. The probability that concurrent workers access the same element is very low. Using finegrained locks can reduce conflicts and improve concurrency.
(c) Pay attention to the difference between read locks andwrite locks.
(d) j may fall in the [i, i+2] range.
(e) Additional thinking: Will deadlock occur? How to avoid?
*/
#include <iostream>
#include <vector>
#include <thread>
#include <shared_mutex> // C17
#include <random>

const int N = 100000; // size of S
const int M = 10; // size of Workers
const int OPERATIONS_SIZE = 10000; // size of Operations
const int SEGMENT_SIZE = 1000; // size of each segment

std::vector<int> S(N);
std::shared_mutex rw_locks[N / SEGMENT_SIZE + 1];  // one lock for each segment

void threadWorker() {
    std::random_device rd;
    std::default_random_engine gen(rd());
    std::uniform_int_distribution<int> dist(0, N - 1);

    for (int op = 0; op < OPERATIONS_SIZE; op++) {
        int i = dist(gen);
        int j = dist(gen);
        int sum = 0;

        int segmentIdx = i / SEGMENT_SIZE; // Calculate the index
        rw_locks[segmentIdx].lock_shared(); // read lock
        rw_locks[(segmentIdx + 1) % (N / SEGMENT_SIZE + 1)].lock_shared();
        rw_locks[(segmentIdx + 2) % (N / SEGMENT_SIZE + 1)].lock_shared();
        
        sum = S[i] + S[(i + 1) % N] + S[(i + 2) % N];
        
        rw_locks[(segmentIdx + 2) % (N / SEGMENT_SIZE + 1)].unlock_shared();
        rw_locks[(segmentIdx + 1) % (N / SEGMENT_SIZE + 1)].unlock_shared();
        rw_locks[segmentIdx].unlock_shared();

        rw_locks[j / SEGMENT_SIZE].lock(); // write lock
        S[j] = sum; 
        rw_locks[j / SEGMENT_SIZE].unlock();
    }
}

int main(void) {
    std::vector<std::thread> workers;
    for (int i = 0; i < N; i++) {
        S[i] = i; // Initialize array
    }

    for (int i = 0; i < M; i++) {
        workers.emplace_back(threadWorker);
    }

    for (auto& worker : workers) {
        worker.join(); // Wait for the thread
    }

    for (int i = 0; i < N; i++) {
        std::cout << S[i] << "\t";
    }
    std::cout <<  std::endl;

    return 0;
}

