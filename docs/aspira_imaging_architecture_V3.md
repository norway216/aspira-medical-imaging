# Medical Imaging AI Architecture V3 (Segmentation + U-Net CPU Inference)

## 1. Overview

This document defines a production-grade medical imaging software architecture inspired by enterprise systems such as GE Healthcare imaging platforms, extended with a full AI segmentation subsystem based on U-Net.

The system supports:
- Real-time imaging pipeline (ultrasound / CT / MRI style abstraction)
- CPU-based U-Net segmentation inference
- Zero-copy frame processing
- Embedded + workstation deployment

---

## 2. System Goals

### 2.1 Functional Goals
- Real-time imaging acquisition
- Frame rendering pipeline
- AI segmentation overlay (U-Net)
- ROI-based inference control
- Study-based workflow management

### 2.2 Non-Functional Goals
- Deterministic latency (<100ms AI loop)
- CPU-first inference design
- Modular micro-process architecture
- Medical-grade reliability

---

## 3. High-Level Architecture

```
+-----------------------------------------------------------+
| UI Layer (Qt/QML)                                        |
| - Image Viewer                                           |
| - Segmentation Overlay                                  |
| - ROI Tools                                             |
+------------------------▲----------------------------------+
                         |
+------------------------|----------------------------------+
| Application Layer                                       |
| - Study Manager                                         |
| - Segmentation Controller                               |
| - AI Model Manager                                      |
+------------------------▲----------------------------------+
                         |
+------------------------|----------------------------------+
| AI Inference Layer (NEW)                                |
| - U-Net CPU Engine                                     |
| - Pre/Post Processing Pipeline                         |
| - ROI Crop Engine                                      |
| - Inference Scheduler                                 |
+------------------------▲----------------------------------+
                         |
+------------------------|----------------------------------+
| Imaging Engine                                          |
| - Beamforming / Reconstruction                        |
| - Filtering / Enhancement                            |
| - Frame Generator                                    |
+------------------------▲----------------------------------+
                         |
+------------------------|----------------------------------+
| Acquisition Layer                                       |
| - Probe / Sensor Input                                |
| - DMA Buffer Management                               |
+-----------------------------------------------------------+
```

---

## 4. Segmentation Subsystem

### 4.1 Segmentation Controller

Responsible for managing AI segmentation lifecycle.

Functions:
- start_segmentation()
- stop_segmentation()
- set_roi()
- switch_model()
- fetch_mask()

---

### 4.2 Segmentation Pipeline

```
Frame Input
   ↓
ROI Extractor
   ↓
Preprocessing
   - resize (256x256 / 512x512)
   - normalize
   ↓
U-Net CPU Inference
   ↓
Postprocessing
   - thresholding
   - morphology cleanup
   ↓
Mask Overlay
   ↓
UI Render
```

---

## 5. U-Net CPU Inference Engine

### 5.1 Design Choice

We use lightweight U-Net for CPU execution:
- <10M parameters
- reduced channel width
- optimized convolution blocks

---

### 5.2 Engine Design

```
UNetInferenceEngine
 ├── load_model()
 ├── preprocess()
 ├── forward_cpu()
 ├── postprocess()
 └── optimize_buffers()
```

---

### 5.3 CPU Optimization

- SIMD acceleration (AVX2 / NEON)
- thread pool inference
- memory reuse (no allocation in loop)
- cache-friendly tensor layout

---

## 6. Dataset Selection (Lightweight Medical AI)

Recommended datasets:

### 6.1 Oxford-IIIT Pet (Debug Dataset)
- Very small (~MB level)
- Good for pipeline validation

### 6.2 LGG Brain MRI Segmentation
- Real medical segmentation task
- moderate size

### 6.3 BraTS Subset (optional)
- Brain tumor segmentation
- can be subsetted for CPU testing

---

## 7. Data Pipeline

```
Dataset
  ↓
Preprocessing
  ↓
Augmentation
  ↓
U-Net Training (PyTorch)
  ↓
ONNX Export
  ↓
CPU Deployment Runtime
```

---

## 8. Runtime Architecture

### Threads

- acquisition_thread
- imaging_thread
- segmentation_trigger_thread
- inference_thread_pool
- ui_thread

---

## 9. Data Structures

```cpp
struct SegmentationResult {
    uint64_t frame_id;
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> mask;
    float confidence;
};
```

---

## 10. Performance Targets

| Module | Target |
|------|--------|
| Preprocess | <5ms |
| U-Net CPU Inference | 30–80ms |
| Postprocess | <5ms |
| Total Latency | <100ms |

---

## 11. Deployment

### Embedded
- ARM RK3588
- Yocto Linux
- CPU inference only

### Workstation
- x86 Linux
- multi-core inference scaling

---

## 12. Summary

This V3 architecture extends GE-like imaging systems with a full CPU-based U-Net segmentation subsystem, enabling real-time medical AI inference in embedded environments.
