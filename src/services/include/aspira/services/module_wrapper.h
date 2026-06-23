/**
 * @file module_wrapper.h
 * @brief RAII C++ wrappers over C core API (uses _create/_free factory functions)
 */

#ifndef ASPIRA_SERVICES_MODULE_WRAPPER_H
#define ASPIRA_SERVICES_MODULE_WRAPPER_H

#include <aspira/core/core.h>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aspira {

class RingBuffer {
public:
    RingBuffer(uint64_t cap, size_t esz) : rb_(aspira_spsc_create(cap, esz)) {
        if (!rb_) throw std::runtime_error("Failed to create SPSC ring buffer");
    }
    ~RingBuffer() { aspira_spsc_free(rb_); }
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    bool push(const void* d) { return aspira_spsc_push(rb_, d); }
    bool pop(void* d) { return aspira_spsc_pop(rb_, d); }
    const void* front() const { return aspira_spsc_front(rb_); }
    void pop_front() { aspira_spsc_pop_front(rb_); }
    uint64_t count() const { return aspira_spsc_count(rb_); }
    bool empty() const { return aspira_spsc_empty(rb_); }
    bool full() const { return aspira_spsc_full(rb_); }
    uint64_t capacity() const { return aspira_spsc_capacity(rb_); }
    aspira_spsc_rb* native() { return rb_; }
    const aspira_spsc_rb* native() const { return rb_; }
private:
    aspira_spsc_rb* rb_;
};

class MemoryPool {
public:
    MemoryPool(size_t bs, size_t n) : p_(aspira_memory_pool_create(bs, n)) {
        if (!p_) throw std::runtime_error("Failed to create memory pool");
    }
    ~MemoryPool() { aspira_memory_pool_free_ptr(p_); }
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    void* alloc() { return aspira_memory_pool_alloc(p_); }
    void free(void* ptr) { aspira_memory_pool_free(p_, ptr); }
    size_t free_count() const { return aspira_memory_pool_free_count(p_); }
    aspira_memory_pool* native() { return p_; }
private:
    aspira_memory_pool* p_;
};

class FramePool {
public:
    FramePool(size_t n, uint32_t w, uint32_t h, uint32_t c = 1)
        : p_(aspira_frame_pool_create(n, w, h, c)) {
        if (!p_) throw std::runtime_error("Failed to create frame pool");
    }
    ~FramePool() { aspira_frame_pool_free_ptr(p_); }
    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;
    aspira_frame* alloc_frame() { return aspira_frame_pool_alloc_frame(p_); }
    void free_frame(aspira_frame* f) { aspira_frame_pool_free_frame(p_, f); }
    size_t free_count() const { return aspira_frame_pool_free_count(p_); }
    aspira_frame_pool* native() { return p_; }
private:
    aspira_frame_pool* p_;
};

class ThreadPool {
public:
    ThreadPool(size_t n, uint64_t qc = 256) : tp_(aspira_thread_pool_create(n, qc)) {
        if (!tp_) throw std::runtime_error("Failed to create thread pool");
    }
    ~ThreadPool() { aspira_thread_pool_free_ptr(tp_); }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    bool enqueue(aspira_priority_t prio, aspira_task_fn func, void* arg) {
        return aspira_thread_pool_enqueue(tp_, prio, func, arg);
    }
    bool enqueue(std::function<void()> task, aspira_priority_t prio = ASPIRA_PRIORITY_PROCESSING);
    bool set_affinity(size_t idx, int core) { return aspira_thread_pool_set_affinity(tp_, idx, core); }
    void wait() { aspira_thread_pool_wait(tp_); }
    uint64_t pending() const { return aspira_thread_pool_pending_count(tp_); }
    aspira_thread_pool* native() { return tp_; }
private:
    aspira_thread_pool* tp_;
};

class DoubleBuffer {
public:
    DoubleBuffer() : db_(aspira_double_buffer_create()) {
        if (!db_) throw std::runtime_error("Failed to create double buffer");
    }
    ~DoubleBuffer() { aspira_double_buffer_free_ptr(db_); }
    void init(aspira_frame* f, aspira_frame* b) { aspira_double_buffer_init(db_, f, b); }
    const aspira_frame* read() { return aspira_double_buffer_read(db_); }
    aspira_frame* write_begin() { return aspira_double_buffer_write_begin(db_); }
    void swap() { aspira_double_buffer_swap(db_); }
    bool has_new_data() const { return aspira_double_buffer_has_new_data(db_); }
    void mark_consumed() { aspira_double_buffer_mark_consumed(db_); }
    aspira_double_buffer* native() { return db_; }
private:
    aspira_double_buffer* db_;
};

class SignalPipeline {
public:
    SignalPipeline() : p_(aspira_pipeline_create()) {
        if (!p_) throw std::runtime_error("Failed to create pipeline");
    }
    ~SignalPipeline() { aspira_pipeline_free_ptr(p_); }
    SignalPipeline(const SignalPipeline&) = delete;
    SignalPipeline& operator=(const SignalPipeline&) = delete;
    bool add_fir(const std::vector<float>& c, const std::string& n = "fir") {
        return aspira_pipeline_add_fir(p_, c.data(), c.size(), n.c_str());
    }
    bool add_envelope(const std::string& n = "env") { return aspira_pipeline_add_envelope(p_, n.c_str()); }
    bool add_downsample(uint32_t f, const std::string& n = "ds") { return aspira_pipeline_add_downsample(p_, f, n.c_str()); }
    bool add_gain(float g, const std::string& n = "gain") { return aspira_pipeline_add_gain(p_, g, n.c_str()); }
    bool add_dc_remove(const std::string& n = "dc") { return aspira_pipeline_add_dc_remove(p_, n.c_str()); }
    bool process(const aspira_frame* in, aspira_frame* out) { return aspira_pipeline_process(p_, in, out); }
    bool process_inplace(aspira_frame* f) { return aspira_pipeline_process_inplace(p_, f); }
    size_t filter_count() const { return aspira_pipeline_filter_count(p_); }
    void reset() { aspira_pipeline_reset(p_); }
    aspira_pipeline* native() { return p_; }
private:
    aspira_pipeline* p_;
};

class Watchdog {
public:
    using CB = std::function<void(const std::string&, aspira_watchdog_state_t, uint64_t, void*)>;
    Watchdog(const std::string& name, uint64_t to_ms, CB cb = {}) {
        if (cb) { cb_ = std::make_unique<CB>(std::move(cb)); cb_ptr_ = cb_.get(); }
        wd_ = aspira_watchdog_create(name.c_str(), to_ms,
              cb_ ? &Watchdog::thunk : nullptr, this);
        if (!wd_) throw std::runtime_error("Failed to create watchdog");
    }
    ~Watchdog() { aspira_watchdog_free_ptr(wd_); }
    void pet() { aspira_watchdog_pet(wd_); }
    aspira_watchdog_state_t state() const { return aspira_watchdog_get_state(wd_); }
    bool is_healthy() const { return aspira_watchdog_is_healthy(wd_); }
    aspira_watchdog* native() { return wd_; }
private:
    static void thunk(const char* n, aspira_watchdog_state_t s, uint64_t hb, void* u) {
        auto* self = static_cast<Watchdog*>(u);
        if (self->cb_ptr_) (*self->cb_ptr_)(n ? n : "", s, hb, nullptr);
    }
    aspira_watchdog* wd_ = nullptr;
    std::unique_ptr<CB> cb_;
    CB* cb_ptr_ = nullptr;
};

class WatchdogManager {
public:
    WatchdogManager(uint64_t iv = 100) : mgr_(aspira_watchdog_manager_create(iv)) {
        if (!mgr_) throw std::runtime_error("Failed to create watchdog manager");
    }
    ~WatchdogManager() { aspira_watchdog_manager_free_ptr(mgr_); }
    bool register_watchdog(Watchdog* wd) { return aspira_watchdog_manager_register(mgr_, wd->native()); }
    bool healthy() const { return aspira_watchdog_manager_healthy(mgr_); }
    aspira_watchdog_manager* native() { return mgr_; }
private:
    aspira_watchdog_manager* mgr_;
};

} // namespace aspira
#endif
