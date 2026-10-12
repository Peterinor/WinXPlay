#include "protocol.h"
namespace cp {
std::vector<Frame> MfiExchange::receive(const Frame &frame) {
    require(state_ != Accepted && state_ != Rejected, "Authentication exchange already ended");
    std::vector<Frame> out;
    try {
        if (frame.id == 0xaa00) {
            Bytes cert = provider_.certificate();
            require(!cert.empty() && cert.size() <= 65525,
                    "Missing or oversized accessory certificate");
            out.push_back(Frame(0xaa01, parameter(0, cert)));
            state_ = CertificateSent;
        } else if (frame.id == 0xaa02) {
            require(state_ == CertificateSent || state_ == ChallengeAnswered,
                    "Challenge before certificate");
            std::vector<Frame> params = parameters(frame.body);
            require(params.size() == 1 && params[0].id == 0 && params[0].body.size() == 32,
                    "Invalid MFi v3 challenge");
            Bytes signature = provider_.signChallenge(params[0].body);
            require(signature.size() == 64, "MFi v3 signature must be raw P256 r||s");
            out.push_back(Frame(0xaa03, parameter(0, signature)));
            state_ = ChallengeAnswered;
        } else if (frame.id == 0xaa05) {
            require(state_ == ChallengeAnswered, "Unsolicited authentication acceptance");
            state_ = Accepted;
        } else if (frame.id == 0xaa04) {
            state_ = Rejected;
            throw std::runtime_error("iPhone rejected accessory identity");
        } else
            require(false, "Unexpected MFi authentication message");
    } catch (...) {
        state_ = Rejected;
        throw;
    }
    return out;
}
} // namespace cp
