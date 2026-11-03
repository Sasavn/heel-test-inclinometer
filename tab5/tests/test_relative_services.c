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
 CHECK(strstr(row,"0.500;-0.250;1;0.100;-0.100;0.400;-0.150;7\n")!=NULL);
 CHECK(tab5_services_format_relative_csv(row,5,&s)<0);
 CHECK(tab5_services_submit(&s)==ESP_OK);media_step(0);media_step(1);
 s.sensor=3;s.relative.ready=false;s.relative.quality=TAB5_PAIR_STALE;
 CHECK(tab5_services_format_relative_csv(row,sizeof(row),&s)>0);
 CHECK(strstr(row,";2;0;1;1.000;2.000;1.500;1.750;;;1;")!=NULL);
 CHECK(!strstr(row,"nan")&&!strstr(row,"inf"));
 CHECK(tab5_services_submit(&s)==ESP_OK);stop_and_drain();
 CHECK(!media[0].relative_file&&!media[1].relative_file&&tab5_services_mode_change_allowed());
 FILE *f=fopen("test-sd/relative_audit_REL.CSV","r");CHECK(f!=NULL);
 CHECK(fgets(row,sizeof(row),f)&&!strcmp(row,TAB5_SERVICE_RELATIVE_HEADER));
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,"1000000;1016000;10;20;")!=NULL);
 CHECK(fgets(row,sizeof(row),f)&&strstr(row,";;;1;")!=NULL);CHECK(fgets(row,sizeof(row),f)==NULL);fclose(f);
 CHECK(tab5_services_record_start("legacy_unchanged")==ESP_OK);CHECK(!media[0].relative_file);stop_and_drain();
 s.sensor=4;CHECK(tab5_services_submit(&s)==ESP_ERR_INVALID_ARG);
 ota.active=true;CHECK(!tab5_services_mode_change_allowed());ota.active=false;
 files_busy=true;CHECK(!tab5_services_mode_change_allowed());files_busy=false;
 transfer.owner=2;CHECK(!tab5_services_mode_change_allowed());transfer.owner=0;
 CHECK(tab5_services_mode_change_allowed());
 printf("relative recording: %u checks PASS: unchanged legacy, extra audit CSV, invalid blanks, IDs/time/offset provenance, dual-media drain, mode/OTA/transfer guard, unsupported head4\n",checks);
 return 0;
}
