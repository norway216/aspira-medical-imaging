/**
 * @file study_manager.cpp
 * @brief Study manager implementation
 */

#include "aspira/services/study_manager.h"

namespace aspira {

bool StudyManager::create_study(const StudyInfo& study) {
    if (study.study_uid.empty()) return false;
    if (studies_.find(study.study_uid) != studies_.end()) return false;
    studies_[study.study_uid] = study;
    return true;
}

bool StudyManager::update_study(const std::string& study_uid,
                                 const StudyInfo& updated) {
    auto it = studies_.find(study_uid);
    if (it == studies_.end()) return false;
    it->second = updated;
    it->second.study_uid = study_uid; /* UID is immutable */
    return true;
}

bool StudyManager::delete_study(const std::string& study_uid) {
    return studies_.erase(study_uid) > 0;
}

StudyInfo* StudyManager::get_study(const std::string& study_uid) {
    auto it = studies_.find(study_uid);
    return it != studies_.end() ? &it->second : nullptr;
}

const StudyInfo* StudyManager::get_study(const std::string& study_uid) const {
    auto it = studies_.find(study_uid);
    return it != studies_.end() ? &it->second : nullptr;
}

std::vector<StudyInfo> StudyManager::query_by_patient(
    const std::string& patient_id) const {
    std::vector<StudyInfo> result;
    for (const auto& [uid, study] : studies_) {
        if (study.patient_id == patient_id) {
            result.push_back(study);
        }
    }
    return result;
}

std::vector<StudyInfo> StudyManager::query_by_status(
    const std::string& status) const {
    std::vector<StudyInfo> result;
    for (const auto& [uid, study] : studies_) {
        if (study.study_status == status) {
            result.push_back(study);
        }
    }
    return result;
}

std::vector<StudyInfo> StudyManager::query_by_modality(
    const std::string& modality) const {
    std::vector<StudyInfo> result;
    for (const auto& [uid, study] : studies_) {
        if (study.modality == modality) {
            result.push_back(study);
        }
    }
    return result;
}

std::vector<StudyInfo> StudyManager::query_by_date(
    const std::string& date) const {
    std::vector<StudyInfo> result;
    for (const auto& [uid, study] : studies_) {
        if (study.study_date == date) {
            result.push_back(study);
        }
    }
    return result;
}

bool StudyManager::add_series(const std::string& study_uid,
                               const SeriesInfo& series) {
    auto* study = get_study(study_uid);
    if (!study) return false;
    study->series_uids.push_back(series.series_uid);
    return true;
}

bool StudyManager::remove_series(const std::string& study_uid,
                                  const std::string& series_uid) {
    auto* study = get_study(study_uid);
    if (!study) return false;
    auto& vec = study->series_uids;
    vec.erase(std::remove(vec.begin(), vec.end(), series_uid), vec.end());
    return true;
}

} // namespace aspira
