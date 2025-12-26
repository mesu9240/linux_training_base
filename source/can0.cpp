#include "can0.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

CANSocket::CANSocket(const std::string &ifname)
    : s_(-1), ifname_(ifname) {}

CANSocket::~CANSocket() {
    if (s_ >= 0) close(s_);
}

bool CANSocket::init() {
    // Configure interface bitrate and bring it up. These commands typically
    // require root privileges; caller should run with sufficient rights.
    int rc;
    std::string cmd;

    // Try to bring interface down (ignore failure)
    cmd = "ip link set " + ifname_ + " down";
    rc = system(cmd.c_str()); (void)rc;

    // Set bitrate to 500000
    cmd = "ip link set " + ifname_ + " type can bitrate 500000";
    rc = system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "Failed to set bitrate for " << ifname_ << " (rc=" << rc << ")\n";
        // continue — the interface may already be configured
    }

    // Bring interface up
    cmd = "ip link set " + ifname_ + " up";
    rc = system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "Failed to bring up " << ifname_ << " (rc=" << rc << ")\n";
        return false;
    }

    // Create socket
    s_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s_ < 0) {
        perror("socket");
        s_ = -1;
        return false;
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, ifname_.c_str(), IFNAMSIZ-1);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if (ioctl(s_, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        close(s_);
        s_ = -1;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(s_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s_);
        s_ = -1;
        return false;
    }

    // Set socket non-blocking to implement non-blocking reads
    int flags = fcntl(s_, F_GETFL, 0);
    if (flags == -1) flags = 0;
    if (fcntl(s_, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL)");
        // Not fatal; reads will use select/recv
    }

    return true;
}

int CANSocket::writeFrame(uint32_t can_id, const char data[8]) {
    if (s_ < 0) return -1;
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = can_id;
    frame.can_dlc = 8;
    std::memcpy(frame.data, data, 8);

    ssize_t n = write(s_, &frame, sizeof(struct can_frame));
    if (n == (ssize_t)sizeof(struct can_frame)) return 1;
    perror("write");
    return -1;
}

int CANSocket::readFrame(uint32_t can_id, char out[8]) {
    if (s_ < 0) return -1;

    // Loop reading available frames (non-blocking). If a matching frame
    // is found, fill `out` and return 1. If no matching frames present,
    // return 0. On error return -1.
    struct can_frame frame;
    while (true) {
        ssize_t n = read(s_, &frame, sizeof(struct can_frame));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data available right now
                return 0;
            }
            perror("read");
            return -1;
        }

        if (n != (ssize_t)sizeof(struct can_frame)) {
            // Partial read or other oddity; ignore and continue
            continue;
        }

        // Extract ID depending on frame format (std vs extended)
        uint32_t frame_id;
        bool is_ext = (frame.can_id & CAN_EFF_FLAG) != 0;
        if (is_ext) frame_id = frame.can_id & CAN_EFF_MASK;
        else frame_id = frame.can_id & CAN_SFF_MASK;

        if (frame_id == can_id && frame.can_dlc == 8) {
            std::memcpy(out, frame.data, 8);
            return 1;
        }

        // Not a matching ID: continue to drain any other pending frames
        // until EAGAIN, but do not block.
    }

    // unreachable
    return 0;
}
