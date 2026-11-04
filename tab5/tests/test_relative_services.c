#define main legacy_services_suite
#include "test_services.c"
#undef main
int main(void){
 _mkdir("test-sd");_mkdir("test-usb");tab5_services_config_t cfg={0};
 CHECK(tab5_services_init(&cfg)==ESP_OK);pump_initial_media();
 CHECK(tab5_services_mode_change_allowed());
 CHECK(tab5_services_record_relative_start("relative_audit")==ESP_OK);
 CHECK(!tab5_services_mode_change_allowed());
 CHECK(media[0].relative_file&&media[1].relative_file);
 tab5_service_sample_t s={.sensor=2,.date="04.11.2026",.time="01:00:00",.raw_x=1,.raw_y=2,.ms=1020,
  .relative_record=true,.relative_config={.version=1,.reference_index=0,.object_index=1,.axes_confirmed=1}};
 s.relative=(tab5_relative_result_t){.ready=true,.offset_valid=true,.source={{1,2},{1.5f,1.75f}},
  .live={.5f,-.25f},.offset={.1f,-.1f},.relative={.4f,-.15f},
  .timestamp_us={1000000,1016000},.sequence={10,20},.age_us={20000,4000},.skew_us=16000,.generation=7};
 char row[640];CHECK(tab5_services_format_relative_csv(row,sizeof(row),&s)>0);
 CHECK(strstr(row,"1020;2;2;3;1000000;1016000;10;20;20000;4000;16000;0;1;1;")!=NULL);
 CHECK(strstr(row,"0.500;-0.250;1;0.100;-0.100;0.400;-0.150;7;SAMPLE;0\n")!=NULL);
 CHECK(tab5_services_format_relative_csv(row,5,&s)<0);
 CHECK(tab5_services_submit(&s,record_epoch)==ESP_OK);media_step(0);media_step(1);
 s.sensor=3;s.relative.ready=false;s.relative.quality=TAB5_PAIR_STALE;
 CHECK(tab5_services_format_relative_csv(row,sizeof(row),&s)>0);
 CHECK(strstr(row,";2;0;1;1.000;2.000;1.500;1.750;;;1;")!=NULL);
 CHECK(!strstr(row,"nan")&&!strstr(row,"inf"));
 CHECK(tab5_services_submit(&s,record_epoch)==ESP_OK);stop_and_drain();
 CHECK(!media[0].relative_file&&!media[1].relative_file&&tab5_services_mode_change_allowed());
 FILE *f=fopen("test-sd/relative_audit_REL.CSV","r");CHECK(f!=NULL);
 CHECK(fgets(row,sizeof(row),f)&&!strcmp(row,TAB5_SERVICE_RELATIVE_HEADER));
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,"1000000;1016000;10;20;")!=NULL);
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,";;;1;")!=NULL);CHECK(fgets(row,sizeof(row),f)==NULL);fclose(f);
 CHECK(tab5_services_record_start("legacy_unchanged")==ESP_OK);CHECK(!media[0].relative_file);stop_and_drain();
 s.sensor=4;CHECK(tab5_services_submit(&s,record_epoch)==ESP_ERR_INVALID_ARG);
 ota.active=true;CHECK(!tab5_services_mode_change_allowed());ota.active=false;
 files_busy=true;CHECK(!tab5_services_mode_change_allowed());files_busy=false;
 transfer.owner=2;CHECK(!tab5_services_mode_change_allowed());transfer.owner=0;
 CHECK(tab5_services_mode_change_allowed());
 CHECK(tab5_services_control_begin());CHECK(!tab5_services_control_begin());
 CHECK(tab5_services_record_start("reserved_control")!=ESP_OK);
 line(1,"files");CHECK(strstr(answer,"ERR")!=NULL);
 line(1,"ota begin tab5 esp32p4 2 1.5.0-esp32 768 00");CHECK(strstr(answer,"ERR")!=NULL);
 CHECK(!tab5_services_mode_change_allowed());tab5_services_control_end();
 CHECK(tab5_services_record_relative_start("workspace_gaps")==ESP_OK);
 s.sensor=2;s.relative_event=true;s.relative_observed_us=9000000;
 CHECK(tab5_services_submit(&s,record_epoch)==ESP_OK);media_step(0);media_step(1);
 tab5_medium_status_t status;tab5_services_status(TAB5_SD,&status);
 CHECK(status.relative_open&&status.relative_rows==1&&status.relative_gaps==1&&status.written==0);
 CHECK(!strcmp(status.relative_session,"workspace_gaps"));
 s.relative.ready=true;s.relative.quality=0;s.relative_observed_us+=100000;
 CHECK(tab5_services_submit(&s,record_epoch)==ESP_OK);
 tab5_services_record_stop();CHECK(!tab5_services_control_begin());tab5_services_status(TAB5_SD,&status);CHECK(status.closing);
 stop_and_drain();tab5_services_status(TAB5_SD,&status);
 CHECK(!status.relative_open&&status.relative_closed_ok&&!status.closing&&status.relative_rows==2&&status.relative_gaps==1);
 f=fopen("test-sd/workspace_gaps_D2.CSV","r");CHECK(f&&fgets(row,sizeof(row),f)&&!strcmp(row,TAB5_SERVICE_CSV_HEADER));CHECK(!fgets(row,sizeof(row),f));fclose(f);
 f=fopen("test-sd/workspace_gaps_REL.CSV","r");CHECK(f&&fgets(row,sizeof(row),f));
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,";GAP;9000000\n"));CHECK(fgets(row,sizeof(row),f)&&strstr(row,";RECOVERY;9100000\n"));fclose(f);
 CHECK(tab5_services_record_relative_start("workspace_write_failure")==ESP_OK);
 s.relative.ready=false;s.relative.quality=TAB5_PAIR_ERROR;test_fault[0]=ENOSPC;
 CHECK(tab5_services_submit(&s,record_epoch)==ESP_OK);media_step(0);media_step(1);
 tab5_services_status(TAB5_SD,&status);CHECK(status.failed&&!status.relative_closed_ok&&!status.relative_open);
 tab5_services_status(TAB5_USB,&status);CHECK(!status.failed&&status.relative_open&&status.relative_rows==1);
 stop_and_drain();test_fault[0]=0;CHECK(retry_and_complete(TAB5_SD)==ESP_OK);
 CHECK(tab5_services_record_relative_start("workspace_close_failure")==ESP_OK);
 test_fault[0]=EIO;stop_and_drain();tab5_services_status(TAB5_SD,&status);
 CHECK(status.failed&&!status.relative_closed_ok&&!status.closing);test_fault[0]=0;
 CHECK(tab5_services_record_epoch()==0);CHECK(retry_and_complete(TAB5_SD)==ESP_OK);
 CHECK(tab5_services_record_relative_start("epoch_A")==ESP_OK);
 uint64_t epoch_a=tab5_services_record_epoch();CHECK(epoch_a!=0);
 s.relative_config.reference_index=0;s.relative_config.object_index=1;s.relative.generation=7;
 stop_and_drain();CHECK(tab5_services_record_relative_start("epoch_B")==ESP_OK);
 uint64_t epoch_b=tab5_services_record_epoch();CHECK(epoch_b&&epoch_b!=epoch_a);
 CHECK(tab5_services_submit(&s,epoch_a)==ESP_ERR_INVALID_STATE); /* Paused router A resumes in B. */
 CHECK(tab5_services_submit(&s,0)==ESP_ERR_INVALID_STATE);CHECK(!media[0].count&&!media[1].count);
 tab5_services_status(TAB5_SD,&status);CHECK(status.relative_rows==0&&status.relative_gaps==0);
 s.relative_config.reference_index=1;s.relative_config.object_index=0;s.relative.generation=9;
 CHECK(tab5_services_submit(&s,epoch_b)==ESP_OK);stop_and_drain();
 f=fopen("test-sd/epoch_B_REL.CSV","r");CHECK(f&&fgets(row,sizeof(row),f));
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,";3;2;")&&strstr(row,";9;GAP;"));CHECK(!fgets(row,sizeof(row),f));fclose(f);
 printf("relative recording: %u checks PASS: unchanged legacy, extra audit CSV, invalid blanks, IDs/time/offset provenance, dual-media drain, mode/OTA/transfer guard, unsupported head4\n",checks);
 return 0;
}
