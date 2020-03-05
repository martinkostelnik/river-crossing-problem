#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/wait.h>

#define mutexName "/mutex"
#define hackerSemName "/hackerSem"
#define serfSemName "/serfSem"
#define cptSemName "/cptSem"
#define exitSemName "/exitSem"

#define pierStatusName "/status"

#define printf(...) fprintf(pierStatus->outFile, __VA_ARGS__)

/**
 * @brief Struct containing program arguments
 * 
 */
typedef struct args
{
    int personCount; // Amount of people that will be generated in each category
    int hackerGenTime_ms; // Time until new hacker is generated
    int serfGenTime_ms; // Time until new serf is generated
    int sailTime_ms; // Ship sail time
    int checkPier_ms; // Time until a person returns
    int pierCapacity; // Port capacity
    int errCode; // Indicates error while parsing arguments
} args_t;

/**
 * @brief Describes the type of a person
 * 
 */
typedef enum
{
    hacker,
    serf
} personType;

/**
 * @brief Contains infromation about the port and also holds output file and actionID
 * 
 */
typedef struct pierStatus
{
    int availableHackers; // Hackers ready to be grouped
    int availableSerfs; // Serfs ready to be grouped
    int groupedHackers; // Hackers already in a group
    int groupedSerfs; // Serfs already in a group
    int actionID; // Unique ID of action
    FILE* outFile; // Output file
} pierStatus_t;

/**
 * @brief Tries to convert string to int
 * 
 * @param s String that will be converted
 * @param outValue Parsed value
 * @return true Returns true if parsing was successful
 * @return false Returns false if parsing was unsuccessful
 */
bool tryParse(const char* const s, int* const outValue)
{
    char *ptr = NULL;
    *outValue = strtol(s, &ptr, 10);
    if (*ptr != '\0')
    {
        return false;
    }

    return true;
}

/**
 * @brief Parses the program arguments
 * 
 * @param argc Argument count
 * @param argv Arguments
 * @return args_t Returns struct with parsed arguments
 */
args_t parseArgs(const int argc, const char* const argv[])
{
    args_t args = { 0 };

    if (argc != 7)
    {
        args.errCode = -1;
        return args;
    }

    if (!tryParse(argv[1], &args.personCount) || args.personCount < 2 || args.personCount & 1 ||
        !tryParse(argv[2], &args.hackerGenTime_ms) || args.hackerGenTime_ms < 0 || args.hackerGenTime_ms > 2000 ||
        !tryParse(argv[3], &args.serfGenTime_ms) || args.serfGenTime_ms < 0 || args.serfGenTime_ms > 2000 ||
        !tryParse(argv[4], &args.sailTime_ms) || args.sailTime_ms < 0 || args.sailTime_ms > 2000 ||
        !tryParse(argv[5], &args.checkPier_ms) || args.checkPier_ms < 20 || args.checkPier_ms > 2000 ||
        !tryParse(argv[6], &args.pierCapacity) || args.pierCapacity < 5)
    {
        args.errCode = -1;
    }

    return args;
}

/**
 * @brief Hacker/Serf routine. Every hacker/serfs checks whether the pier is full, if so, it waits. Otherwise joins the others
 *        and tries to form a group. The process that finds a group becomes captain and sails the ship.
 * 
 * @param type Hacker / Serf
 * @param pierCapacity Capacity of the pier
 * @param checkPier_ms Return time to see if there is empty space on the pier
 * @param sailTime_ms Sail time
 * @param index Process index
 */
void run(const personType type, const int pierCapacity, const int checkPier_ms, const int sailTime_ms, const int index)
{
    const int checkPier_us = checkPier_ms * 1000;
    const int sailTime_us = sailTime_ms * 1000;

    srand(time(NULL)); // random seed

    // SEMAMPHORED AND SHARED MEMORY INIT*********************************
    sem_t* mutex = sem_open(mutexName, O_RDWR);
    sem_t* hackerQueue = sem_open(hackerSemName, O_RDWR);
    sem_t* serfQueue = sem_open(serfSemName, O_RDWR);
    sem_t* cptMutex = sem_open(cptSemName, O_RDWR);
    sem_t* exitBarrier = sem_open(exitSemName, O_RDWR);

    int shmStatus_fd = shm_open(pierStatusName, O_RDWR, S_IRUSR | S_IWUSR);
    pierStatus_t* pierStatus = (pierStatus_t*)mmap(NULL, sizeof(pierStatus_t), PROT_READ | PROT_WRITE, MAP_SHARED, shmStatus_fd, 0);
    close(shmStatus_fd);
    //********************************************************************

    sem_wait(mutex);
    printf("%d: %s %d: starts\n", pierStatus->actionID++, (type == hacker ? "HACK" : "SERF"), index);
    sem_post(mutex);

    while(true)
    {
        if (pierStatus->availableHackers + pierStatus->availableSerfs >= pierCapacity) // Check if pier full
        {
            sem_wait(mutex);
            printf("%d: %s %d: leaves queue: %d: %d\n", pierStatus->actionID++, (type == hacker ? "HACK" : "SERF"), index, pierStatus->availableHackers, pierStatus->availableSerfs);
            sem_post(mutex);
            usleep((rand() % (checkPier_us - 19)) + 20);
            sem_wait(mutex);
            printf("%d: %s %d: is back\n", pierStatus->actionID++, (type == hacker ? "HACK" : "SERF"), index);
            sem_post(mutex);
        }
        else // Enters pier
        {
            if (type == hacker) // hacker
            {
                sem_wait(mutex);
                printf("%d: HACK %d: waits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers + 1, pierStatus->availableSerfs);

                pierStatus->availableHackers++;

                if (pierStatus->availableHackers - pierStatus->groupedHackers >= 4)
                {
                    pierStatus->groupedHackers += 4;
                    sem_post(mutex);
                    sem_wait(cptMutex);
                    sem_wait(mutex);
                    pierStatus->availableHackers -= 4;
                    pierStatus->groupedHackers -= 4;
                    
                    printf("%d: HACK %d: boards: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    
                    sem_post(mutex);
                    usleep(rand() % (sailTime_us + 1));
                    sem_post(hackerQueue);
                    sem_post(hackerQueue);
                    sem_post(hackerQueue);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(mutex);
                    
                    printf("%d: HACK %d: captain exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(cptMutex);
                }
                else if (pierStatus->availableHackers - pierStatus->groupedHackers >= 2 && pierStatus->availableSerfs - pierStatus->groupedSerfs >= 2)
                {
                    pierStatus->groupedHackers += 2;
                    pierStatus->groupedSerfs += 2;
                    sem_post(mutex);
                    sem_wait(cptMutex);
                    sem_wait(mutex);
                    pierStatus->availableSerfs -= 2;
                    pierStatus->availableHackers -= 2;
                    pierStatus->groupedHackers -= 2;
                    pierStatus->groupedSerfs -= 2;
                    printf("%d: HACK %d: boards: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(mutex);
                    usleep(rand() % (sailTime_us + 1));
                    sem_post(hackerQueue);
                    sem_post(serfQueue);
                    sem_post(serfQueue);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(mutex);
                    printf("%d: HACK %d: captain exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(cptMutex);
                }
                else
                {
                    sem_post(mutex);
                    sem_wait(hackerQueue);
                    sem_wait(mutex);
                    printf("%d: HACK %d: member exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(exitBarrier);
                }

                sem_post(mutex);
                break;
            }
            else // serf
            {
                sem_wait(mutex);
                printf("%d: SERF %d: waits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs + 1);
                
                pierStatus->availableSerfs++;

                if (pierStatus->availableSerfs - pierStatus->groupedSerfs >= 4)
                {
                    pierStatus->groupedSerfs += 4;
                    sem_post(mutex);
                    sem_wait(cptMutex);
                    sem_wait(mutex);
                    pierStatus->availableSerfs -= 4;
                    pierStatus->groupedSerfs -= 4;
                    printf("%d: SERF %d: boards: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(mutex);
                    usleep(rand() % (sailTime_us + 1));
                    sem_post(serfQueue);
                    sem_post(serfQueue);
                    sem_post(serfQueue);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(mutex);
                    printf("%d: SERF %d: captain exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(cptMutex);
                }
                else if (pierStatus->availableHackers - pierStatus->groupedHackers >= 2 && pierStatus->availableSerfs - pierStatus->groupedSerfs >= 2)
                {
                    pierStatus->groupedHackers += 2;
                    pierStatus->groupedSerfs += 2;
                    sem_post(mutex);
                    sem_wait(cptMutex);
                    sem_wait(mutex);

                    pierStatus->availableHackers -= 2;
                    pierStatus->availableSerfs -= 2;
                    pierStatus->groupedHackers -= 2;
                    pierStatus->groupedSerfs -= 2;
                    printf("%d: SERF %d: boards: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(mutex);
                    usleep(rand() % (sailTime_us + 1));
                    sem_post(hackerQueue);
                    sem_post(hackerQueue);
                    sem_post(serfQueue);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(exitBarrier);
                    sem_wait(mutex);
                    printf("%d: SERF %d: captain exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(cptMutex);
                }
                else
                {
                    sem_post(mutex);
                    sem_wait(serfQueue);
                    sem_wait(mutex);
                    printf("%d: SERF %d: member exits: %d: %d\n", pierStatus->actionID++, index, pierStatus->availableHackers, pierStatus->availableSerfs);
                    sem_post(exitBarrier);
                }
                sem_post(mutex);
                break;
            }   
        }
    }

    munmap(pierStatus, sizeof(pierStatus_t));

    sem_close(mutex);
    sem_close(hackerQueue);
    sem_close(serfQueue);
    sem_close(cptMutex);
    sem_close(exitBarrier);

    exit(0);
}

/**
 * @brief Generator process. 
 * 
 * @param type Hacker / Serf
 * @param args Program arguments
 */
void generate(const personType type, const args_t * const args)
{
    srand(time(NULL)); // Random seed

    const int maxSleeptime_us = (type == hacker ? args->hackerGenTime_ms : args->serfGenTime_ms) * 1000;

    int* pid = (int*)malloc(sizeof(int) * args->personCount);
    
    for (int i = 0; i < args->personCount; i++) // Generate
    {
        usleep(rand() % (maxSleeptime_us + 1)); // Wait between generating

        if ((pid[i] = fork()) < 0)
        {
            perror("fork");
            free(pid);
        }
        else if (pid[i] == 0) // NEW HACKER/SERF
        {
            run(type, args->pierCapacity, args->checkPier_ms, args->sailTime_ms, i + 1);
        }
    }

    for(int i = 0; i < args->personCount; i++) // Wait for children to die
    {
        waitpid(pid[i], NULL, 0);
    }

    free(pid);
    exit(0);
}

/**
 * @brief Entry point of the application
 * 
 * @param argc Argument count
 * @param argv Arguments
 * @return int Return value
 */
int main(const int argc, const char* const argv[])
{
    const args_t args = parseArgs(argc, argv); // Parse args
    if (args.errCode == -1)
    {
        fprintf(stderr, "ERROR PARSING ARGUMENTS\n");
        return 1;
    }

    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    int pid = 0;

    // SEMAPHORES AND SHARED MEMORY*******************************************************
    sem_t* mutex = sem_open(mutexName, O_CREAT, 0666, 1);
    sem_close(mutex);
    sem_t* hackerQueue = sem_open(hackerSemName, O_CREAT, 0666, 0);
    sem_close(hackerQueue);
    sem_t* serfQueue = sem_open(serfSemName, O_CREAT, 0666, 0);
    sem_close(serfQueue);
    sem_t* captain = sem_open(cptSemName, O_CREAT, 0666, 1);
    sem_close(captain);
    sem_t* exitBarrier = sem_open(exitSemName, O_CREAT, 0666, 0);
    sem_close(exitBarrier);

    int shmStatus_fd = shm_open(pierStatusName, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    ftruncate(shmStatus_fd, sizeof(pierStatus_t));

    pierStatus_t*  pierStatus = (pierStatus_t*)mmap(NULL, sizeof(pierStatus_t), PROT_READ | PROT_WRITE, MAP_SHARED, shmStatus_fd, 0);
    close(shmStatus_fd);
    pierStatus->availableHackers = 0;
    pierStatus->availableSerfs = 0;
    pierStatus->actionID = 1;
    pierStatus->groupedHackers = 0;
    pierStatus->groupedSerfs = 0;
    FILE* f = pierStatus->outFile = fopen("proj2.out", "w");
    if (f == NULL)
    {
        perror("fopen");
        return 1;
    }
    setbuf(pierStatus->outFile, NULL);
    munmap(pierStatus, sizeof(pierStatus));
    //*************************************************************************************

    if ((pid = fork()) < 0) // Create first generator
    {
        perror("fork");
    }

    if (pid == 0) // hacker generator process
    {
        generate(hacker, &args);
    }
    else if (pid > 0) // main process
    {
        if ((pid = fork()) < 0) // Create second generator
        {
            perror("fork");
        }
        else if (pid == 0) // serf generator process
        {
            generate(serf, &args);
        }
    }

    // Wait for children to die
    wait(NULL);
    wait(NULL);

    shm_unlink(pierStatusName);

    sem_unlink(mutexName);
    sem_unlink(hackerSemName);
    sem_unlink(serfSemName);
    sem_unlink(cptSemName);
    sem_unlink(exitSemName);

    fclose(f);

    return 0;
}
