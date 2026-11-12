// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef S7NET_INCLUDE_S7_NET_FRAMES_HPP
#define S7NET_INCLUDE_S7_NET_FRAMES_HPP

#include <s7/net.hpp>

namespace s7 {
// One graph evaluation transfers one frame across all declared channel ports.
// Packet batching never changes the logical sample rate. A failed domain sample
// latches this boundary; restarting it requires a new graph/stream identity.
class NetFrameElement : public Element {
public:
    ~NetFrameElement() override;
    [[nodiscard]] std::optional<Rate> declared_rate() const override;
    [[nodiscard]] const transport::FrameStreamConfig& frame_config() const { return config_; }
    void on_step_failure() noexcept override;
protected:
    NetFrameElement(std::string_view name, transport::FrameStreamConfig config, transport::StreamRole role);
    void ensure_stream();
    [[noreturn]] void fail(frames::Status status);
    transport::FrameStreamConfig config_;
    transport::StreamRole role_;
    transport::RuntimeNetEndpointPtr endpoint_;
    transport::StreamHandle handle_;
    std::shared_ptr<void> storage_; // actual initialized objects of the channel type
    bool failed_{};
};

class NetFrameSink final : public NetFrameElement {
public:
    explicit NetFrameSink(transport::FrameStreamConfig config);
    void step() override;
};
class NetFrameSource final : public NetFrameElement {
public:
    explicit NetFrameSource(transport::FrameStreamConfig config);
    void step() override;
    [[nodiscard]] const frames::Info& last_frame_info() const { return last_info_; }
private:
    frames::Info last_info_;
};
void register_net_frame_elements(ElementRegistry& registry);
} // namespace s7
#endif
