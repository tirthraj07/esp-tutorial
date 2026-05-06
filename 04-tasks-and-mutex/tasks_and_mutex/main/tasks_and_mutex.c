#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// =========================================================
// CHANGE THIS TO 1 TO FIX THE RACE CONDITION!
// =========================================================
#define USE_MUTEX 1

int shared_bank_account = 0;

SemaphoreHandle_t atm_mutex;


void deposit_task(void* pvParameters){
    char* task_name = (char*)pvParameters;

    for (int i = 0; i < 5; i++){

        if (USE_MUTEX){
            // Wait indefinitely until the mutex key is available, then lock the door
            xSemaphoreTake(atm_mutex, portMAX_DELAY);
        }

        // --- THE CRITICAL SECTION ---
        // 1. Read the current balance into local memory
        int local_balance = shared_bank_account;
        // 2. FORCE A CONTEXT SWITCH!
        // We tell this task to sleep for 10ms right in the middle of the transaction.
        // The FreeRTOS scheduler will instantly give control to the other task.
        vTaskDelay(10 / portTICK_PERIOD_MS);

        // 3. Add $100 and save it back to the global variable
        local_balance += 100;
        shared_bank_account = local_balance;


        printf("%s deposited $100. New Balance: $%d\n", task_name, shared_bank_account);


        if (USE_MUTEX) {
            // Unlock the door and give the key back
            xSemaphoreGive(atm_mutex);
        }

        // Wait a bit before trying to deposit again
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Tasks in FreeRTOS must explicitly delete themselves when finished
    vTaskDelete(NULL);
}


void app_main(void) {
    printf("\n=== FreeRTOS Race Condition Explorer ===\n");
    printf("Using Mutex: %s\n\n", USE_MUTEX ? "YES" : "NO");


    // 1. Create the Mutex token
    atm_mutex = xSemaphoreCreateMutex();

    // 2. Create two separate tasks (threads) that run the exact same function.
    // They both have the same priority (1), so the OS will constantly switch between them.
    xTaskCreate(deposit_task, "Task A", 2048, "Task A", 1, NULL);
    xTaskCreate(deposit_task, "Task B", 2048, "Task B", 1, NULL);

}

/*
Without USE_MUTEX
Task A deposited $100. New Balance: $100
Task B deposited $100. New Balance: $100
Task A deposited $100. New Balance: $200
Task B deposited $100. New Balance: $200
Task A deposited $100. New Balance: $300
Task B deposited $100. New Balance: $300
Task A deposited $100. New Balance: $400
Task B deposited $100. New Balance: $400
Task A deposited $100. New Balance: $500
Task B deposited $100. New Balance: $500


With USE_MUTEX
Task A deposited $100. New Balance: $100
Task B deposited $100. New Balance: $200
Task A deposited $100. New Balance: $300
Task B deposited $100. New Balance: $400
Task A deposited $100. New Balance: $500
Task B deposited $100. New Balance: $600
Task A deposited $100. New Balance: $700
Task B deposited $100. New Balance: $800
Task A deposited $100. New Balance: $900
Task B deposited $100. New Balance: $1000
*/