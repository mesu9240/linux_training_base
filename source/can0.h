#ifndef CAN0_H
#define CAN0_H

#include <string>
#include <cstdint>

class CANSocket {
public:
    explicit CANSocket(const std::string &ifname = "can0");
    ~CANSocket();

    // Initialize interface to 500k and bring it up, then open and bind socket
    // Returns true on success
    bool init();

    // Write a CAN frame with given ID and 8-byte payload
    // Returns 1 on success, -1 on error
    int writeFrame(uint32_t can_id, const char data[8]);

    // Non-blocking read for a specific can_id.
    // If a matching frame is found, fills `out` and returns 1.
    // If no data available (or nothing matching) returns 0.
    // On error returns -1.
    int readFrame(uint32_t can_id, char out[8]);

private:
    int s_; // socket fd
    std::string ifname_;
};

#endif // CAN0_H
