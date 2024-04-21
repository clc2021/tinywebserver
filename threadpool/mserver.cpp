#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    // 创建监听套接字
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return -1;
    }

    // 绑定套接字到地址和端口
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // 监听所有网络接口
    server_addr.sin_port = htons(5005);       // 监听端口5005

    if (bind(listen_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    // 开始监听连接请求
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    std::cout << "Server listening on port 5005..." << std::endl;

    // 接受客户端连接
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_addr_len);
    if (client_fd == -1) {
        perror("accept");
        close(listen_fd);
        return -1;
    }

    std::cout << "Accepted connection from " << inet_ntoa(client_addr.sin_addr) << std::endl;

    // 与客户端通信
    char buffer[1024];
    ssize_t recv_len = recv(client_fd, buffer, sizeof(buffer), 0);
    if (recv_len == -1) {
        perror("recv");
    } else if (recv_len == 0) {
        std::cout << "Connection closed by client." << std::endl;
    } else {
        buffer[recv_len] = '\0';
        std::cout << "Received message from client: " << buffer << std::endl;
    }

    // 关闭连接
    close(client_fd);
    close(listen_fd);

    return 0;
}
