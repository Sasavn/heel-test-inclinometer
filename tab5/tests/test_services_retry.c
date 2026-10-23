/* Exercise production API and its real service lock with injected I/O. */
#define main service_core_suite_main
#include "test_services.c"
#undef main

int main(void) {
    CHECK(tab5_services_set_auto_retry(TAB5_SD,false)==ESP_ERR_INVALID_STATE);
    CHECK(tab5_services_set_auto_retry((tab5_medium_t)2,false)==ESP_ERR_INVALID_ARG);
    _mkdir("test-sd");_mkdir("test-usb");
    tab5_services_config_t cfg={0};CHECK(tab5_services_init(&cfg)==ESP_OK);
    CHECK(auto_retry[0]&&auto_retry[1]);
    test_present[TAB5_SD]=false;
    test_now=10000;CHECK(tab5_services_retry(TAB5_SD)==ESP_OK);CHECK(mount_requested[TAB5_SD]);storage_worker_step(TAB5_SD);CHECK(media[TAB5_SD].status.failed&&media[TAB5_SD].status.error==ESP_FAIL);
    CHECK(sd_retry.at_ms==10000&&sd_retry.wait_ms==2000);
    test_now=12000;CHECK(tab5_services_retry(TAB5_SD)==ESP_OK);CHECK(mount_requested[TAB5_SD]);storage_worker_step(TAB5_SD);CHECK(media[TAB5_SD].status.failed&&media[TAB5_SD].status.error==ESP_FAIL);
    CHECK(sd_retry.wait_ms==4000);
    tab5_retry_backoff_t previous=sd_retry;
    tab5_medium_status_t before=media[TAB5_SD].status;
    /* A diagnostic toggle is allowed even during recording/file activity.
     * It only changes policy; never invokes a mount/unmount/record operation. */
    recording=true;files_busy=true;
    CHECK(tab5_services_set_auto_retry(TAB5_SD,false)==ESP_OK);
    CHECK(!auto_retry[0]&&auto_retry[1]&&recording&&files_busy);
    CHECK(!memcmp(&before,&media[TAB5_SD].status,sizeof(before)));
    CHECK(!memcmp(&previous,&sd_retry,sizeof(previous)));
    test_now=20000;
    CHECK(tab5_services_set_auto_retry(TAB5_SD,true)==ESP_OK);
    CHECK(auto_retry[0]&&sd_retry.at_ms==20000&&sd_retry.wait_ms==2000&&sd_retry.next_wait_ms==2000);
    CHECK(recording&&files_busy&&!memcmp(&before,&media[0].status,sizeof(before)));
    recording=files_busy=false;
    CHECK(tab5_services_set_auto_retry(TAB5_SD,false)==ESP_OK);
    test_present[TAB5_SD]=true;
    CHECK(tab5_services_retry(TAB5_SD)==ESP_OK);CHECK(mount_requested[TAB5_SD]);storage_worker_step(TAB5_SD); /* Manual retry remains explicit while auto retry is off. */
    CHECK(media[0].status.mounted&&!auto_retry[0]&&sd_retry.next_wait_ms==2000);
    CHECK(tab5_services_set_auto_retry(TAB5_USB,false)==ESP_OK);
    CHECK(!auto_retry[1]);
    CHECK(!test_lock_depth);
    puts("service retry API: defaults/validation/lock, recording unaffected, enable reset, disable preserves, manual retry PASS");
    return 0;
}
