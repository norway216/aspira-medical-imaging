/**
 * @file study_manager.h
 * @brief DICOM-compatible study management
 */

#ifndef ASPIRA_SERVICES_STUDY_MANAGER_H
#define ASPIRA_SERVICES_STUDY_MANAGER_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace aspira {

struct StudyInfo {
    std::string study_uid;
    std::string study_id;
    std::string study_date;
    std::string study_time;
    std::string study_description;
    std::string modality;           /* US, CT, MR, XA */
    std::string patient_id;
    std::string referring_physician;
    std::string accession_number;
    std::string study_status;       /* SCHEDULED, IN_PROGRESS, COMPLETED, REPORTED */

    /* Series within this study */
    std::vector<std::string> series_uids;
};

struct SeriesInfo {
    std::string series_uid;
    std::string series_number;
    std::string series_description;
    std::string modality;
    uint32_t    num_images = 0;
    std::string body_part;
};

class StudyManager {
public:
    StudyManager() = default;

    /* Study CRUD */
    bool create_study(const StudyInfo& study);
    bool update_study(const std::string& study_uid, const StudyInfo& updated);
    bool delete_study(const std::string& study_uid);
    StudyInfo* get_study(const std::string& study_uid);
    const StudyInfo* get_study(const std::string& study_uid) const;

    /* Query */
    std::vector<StudyInfo> query_by_patient(const std::string& patient_id) const;
    std::vector<StudyInfo> query_by_status(const std::string& status) const;
    std::vector<StudyInfo> query_by_modality(const std::string& modality) const;
    std::vector<StudyInfo> query_by_date(const std::string& date) const;

    /* Series management */
    bool add_series(const std::string& study_uid, const SeriesInfo& series);
    bool remove_series(const std::string& study_uid, const std::string& series_uid);

    size_t study_count() const { return studies_.size(); }

private:
    std::unordered_map<std::string, StudyInfo> studies_;
};

} // namespace aspira

#endif /* ASPIRA_SERVICES_STUDY_MANAGER_H */
