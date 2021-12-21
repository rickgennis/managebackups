
#include "BackupConfig.h"

BackupConfig::BackupConfig() {
    title = directory = filename = backup_command = cp_to = sftp_to = "";
    failsafe_backups = failsafe_days = 0;
    default_days = 14;
    default_weeks = 4;
    default_months = 6;
    default_years = 1;
}
