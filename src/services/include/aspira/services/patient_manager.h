/**
 * @file patient_manager.h
 * @brief Patient record management
 */

#ifndef ASPIRA_SERVICES_PATIENT_MANAGER_H
#define ASPIRA_SERVICES_PATIENT_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>

namespace aspira {

struct PatientInfo {
    std::string patient_id;
    std::string patient_name;
    std::string birth_date;
    std::string sex;           /* M, F, O */
    std::string medical_record_number;
    std::string accession_number;

    /* Demographics */
    float       weight_kg = 0.0f;
    float       height_cm = 0.0f;
    std::string allergies;
    std::string notes;
};

class PatientManager {
public:
    PatientManager() = default;

    bool add_patient(const PatientInfo& patient);
    bool update_patient(const std::string& patient_id,
                        const PatientInfo& updated);
    bool remove_patient(const std::string& patient_id);
    PatientInfo* get_patient(const std::string& patient_id);
    const PatientInfo* get_patient(const std::string& patient_id) const;

    std::vector<PatientInfo> query_by_name(const std::string& name) const;
    std::vector<PatientInfo> query_by_mrn(const std::string& mrn) const;
    std::vector<PatientInfo> all_patients() const;

    size_t patient_count() const { return patients_.size(); }

private:
    std::unordered_map<std::string, PatientInfo> patients_;
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_PATIENT_MANAGER_H */
