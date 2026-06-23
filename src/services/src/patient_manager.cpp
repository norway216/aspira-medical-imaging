/**
 * @file patient_manager.cpp
 * @brief Patient manager implementation
 */

#include "aspira/services/patient_manager.h"

namespace aspira {

bool PatientManager::add_patient(const PatientInfo& patient) {
    if (patient.patient_id.empty()) return false;
    if (patients_.find(patient.patient_id) != patients_.end()) return false;
    patients_[patient.patient_id] = patient;
    return true;
}

bool PatientManager::update_patient(const std::string& patient_id,
                                     const PatientInfo& updated) {
    auto it = patients_.find(patient_id);
    if (it == patients_.end()) return false;
    it->second = updated;
    it->second.patient_id = patient_id;
    return true;
}

bool PatientManager::remove_patient(const std::string& patient_id) {
    return patients_.erase(patient_id) > 0;
}

PatientInfo* PatientManager::get_patient(const std::string& patient_id) {
    auto it = patients_.find(patient_id);
    return it != patients_.end() ? &it->second : nullptr;
}

const PatientInfo* PatientManager::get_patient(
    const std::string& patient_id) const {
    auto it = patients_.find(patient_id);
    return it != patients_.end() ? &it->second : nullptr;
}

std::vector<PatientInfo> PatientManager::query_by_name(
    const std::string& name) const {
    std::vector<PatientInfo> result;
    for (const auto& [id, patient] : patients_) {
        if (patient.patient_name.find(name) != std::string::npos) {
            result.push_back(patient);
        }
    }
    return result;
}

std::vector<PatientInfo> PatientManager::query_by_mrn(
    const std::string& mrn) const {
    std::vector<PatientInfo> result;
    for (const auto& [id, patient] : patients_) {
        if (patient.medical_record_number == mrn) {
            result.push_back(patient);
        }
    }
    return result;
}

std::vector<PatientInfo> PatientManager::all_patients() const {
    std::vector<PatientInfo> result;
    for (const auto& [id, patient] : patients_) {
        result.push_back(patient);
    }
    return result;
}

} // namespace aspira
