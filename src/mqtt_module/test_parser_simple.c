/**
 * @file test_parser_simple.c
 * @brief Simple demonstration of MQTT message parser usage
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Example of how the parser would be used */
void demonstrate_mqtt_parser(void)
{
    printf("MQTT Message Parser (RDP-119) - Usage Example\n");
    printf("=============================================\n\n");
    
    /* Example 1: Standard audio alert message */
    printf("Example 1: Standard Audio Alert Message\n");
    printf("---------------------------------------\n");
    
    const char *json_header = 
        "{"
        "\"opus_data_size\":2048,"
        "\"priority\":10,"
        "\"save_to_file\":false,"
        "\"play_count\":1,"
        "\"volume\":90,"
        "\"interrupt_current\":true"
        "}";
    
    printf("JSON Header:\n%s\n", json_header);
    printf("Followed by 2048 bytes of Opus audio data\n\n");
    
    /* Example 2: Save to file message */
    printf("Example 2: Save Audio to File\n");
    printf("-----------------------------\n");
    
    const char *json_save = 
        "{"
        "\"opus_data_size\":4096,"
        "\"priority\":5,"
        "\"save_to_file\":true,"
        "\"filename\":\"emergency_alert.opus\","
        "\"play_count\":3,"
        "\"volume\":100"
        "}";
    
    printf("JSON Header:\n%s\n", json_save);
    printf("Followed by 4096 bytes of Opus audio data\n");
    printf("Will be saved as: /lfs/emergency_alert.opus\n\n");
    
    /* Example 3: Continuous playback */
    printf("Example 3: Continuous Playback\n");
    printf("------------------------------\n");
    
    const char *json_continuous = 
        "{"
        "\"opus_data_size\":1024,"
        "\"priority\":15,"
        "\"play_count\":0,"  // 0 means infinite
        "\"volume\":75,"
        "\"interrupt_current\":true"
        "}";
    
    printf("JSON Header:\n%s\n", json_continuous);
    printf("play_count=0 means infinite playback until stopped\n\n");
    
    /* Show message structure */
    printf("MQTT Message Structure:\n");
    printf("----------------------\n");
    printf("[JSON Header][Binary Opus Data]\n");
    printf("     |              |\n");
    printf("     |              +-- Size specified in JSON\n");
    printf("     +-- Variable length, ends with '}'\n\n");
    
    printf("Parsed Metadata Fields:\n");
    printf("- opus_data_size (required): Size of Opus data in bytes\n");
    printf("- priority: 0-255, higher = more important\n");
    printf("- save_to_file: Whether to save audio to filesystem\n");
    printf("- filename: Target filename if save_to_file=true\n");
    printf("- play_count: Times to play (0=infinite)\n");
    printf("- volume: 0-100 percent\n");
    printf("- interrupt_current: Stop current playback\n");
}

int main(void)
{
    demonstrate_mqtt_parser();
    
    printf("\nImplementation Files:\n");
    printf("- mqtt_message_parser.h/c - Core parser\n");
    printf("- mqtt_audio_handler.h/c - MQTT integration\n");
    printf("- test_mqtt_parser.c - Unit tests\n");
    
    return 0;
}
