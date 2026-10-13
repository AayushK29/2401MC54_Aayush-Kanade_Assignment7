#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define N 5
#define M 3

// Display a matrix.
void
print_matrix(char *name, int matrix[N][M])
{
  printf("%s:\n", name);

  for(int i = 0; i < N; i++){
    printf("P%d: ", i);

    for(int j = 0; j < M; j++)
      printf("%d ", matrix[i][j]);

    printf("\n");
  }
}

// Display currently available resources.
void
print_available(int available[M])
{
  printf("Available: ");

  for(int j = 0; j < M; j++)
    printf("%d ", available[j]);

  printf("\n");
}

// Deadlock detection algorithm.
// Returns the number of deadlocked processes.
// Stores their process indices in deadlocked_list[].
int
detect_deadlock(int allocation[N][M],
                int request[N][M],
                int available[M],
                int deadlocked_list[N])
{
  int work[M];
  int finish[N];
  int deadlocked_count = 0;

  for(int j = 0; j < M; j++)
    work[j] = available[j];

  // Processes holding no resources can be considered finished.
  for(int i = 0; i < N; i++){
    finish[i] = 1;

    for(int j = 0; j < M; j++){
      if(allocation[i][j] != 0){
        finish[i] = 0;
        break;
      }
    }
  }

  printf("\nStarting deadlock detection...\n");
  print_available(work);

  for(;;){
    int found = 0;

    for(int i = 0; i < N; i++){
      if(finish[i])
        continue;

      int possible = 1;

      for(int j = 0; j < M; j++){
        if(request[i][j] > work[j]){
          possible = 0;
          break;
        }
      }

      if(possible){
        printf("P%d can finish; releasing its resources.\n", i);

        for(int j = 0; j < M; j++)
          work[j] += allocation[i][j];

        finish[i] = 1;
        found = 1;

        print_available(work);
      }
    }

    if(!found)
      break;
  }

  for(int i = 0; i < N; i++){
    if(!finish[i]){
      deadlocked_list[deadlocked_count] = i;
      deadlocked_count++;
    }
  }

  printf("\nDeadlocked processes: ");

  if(deadlocked_count == 0)
    printf("None");

  for(int i = 0; i < deadlocked_count; i++)
    printf("P%d ", deadlocked_list[i]);

  printf("\n");

  if(deadlocked_count)
    printf("System is DEADLOCKED.\n");
  else
    printf("System is NOT DEADLOCKED.\n");

  return deadlocked_count;
}

// Banker's Algorithm safety check.
// Returns 1 for a safe state and 0 for an unsafe state.
int
is_safe_state(int allocation[N][M],
              int need[N][M],
              int available[M])
{
  int work[M];
  int finish[N];
  int count = 0;

  for(int j = 0; j < M; j++)
    work[j] = available[j];

  for(int i = 0; i < N; i++)
    finish[i] = 0;

  printf("\nChecking system safety...\n");
  print_available(work);
  printf("Safe sequence: ");

  while(count < N){
    int found = 0;

    for(int i = 0; i < N; i++){
      if(finish[i])
        continue;

      int possible = 1;

      for(int j = 0; j < M; j++){
        if(need[i][j] > work[j]){
          possible = 0;
          break;
        }
      }

      if(possible){
        printf("P%d ", i);

        for(int j = 0; j < M; j++)
          work[j] += allocation[i][j];

        finish[i] = 1;
        count++;
        found = 1;

        print_available(work);
      }
    }

    if(!found)
      break;
  }

  printf("\n");

  if(count == N){
    printf("System is SAFE.\n");
    return 1;
  }

  printf("System is UNSAFE.\n");
  return 0;
}

// Test 1: A deadlocked state.
void
test_deadlock(void)
{
  int allocation[N][M] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
    {0, 0, 0},
    {0, 0, 0}
  };

  int request[N][M] = {
    {0, 1, 0},
    {0, 0, 1},
    {1, 0, 0},
    {0, 0, 0},
    {0, 0, 0}
  };

  int available[M] = {0, 0, 0};
  int deadlocked_list[N];

  printf("\n========== TEST 1: DEADLOCK ==========\n");

  print_matrix("Allocation", allocation);
  print_matrix("Request", request);
  print_available(available);

  int count = detect_deadlock(
      allocation, request, available, deadlocked_list);

  printf("Number of deadlocked processes: %d\n", count);
}

// Test 2: A safe state.
void
test_safe(void)
{
  int allocation[N][M] = {
    {0, 1, 0},
    {2, 0, 0},
    {3, 0, 2},
    {2, 1, 1},
    {0, 0, 2}
  };

  int need[N][M] = {
    {7, 4, 3},
    {1, 2, 2},
    {6, 0, 0},
    {0, 1, 1},
    {4, 3, 1}
  };

  int available[M] = {3, 3, 2};

  printf("\n========== TEST 2: SAFE STATE ==========\n");

  print_matrix("Allocation", allocation);
  print_matrix("Need", need);
  print_available(available);

  is_safe_state(allocation, need, available);
}

// Test 3: An unsafe request.
void
test_unsafe_request(void)
{
  int allocation[N][M] = {
    {0, 1, 0},
    {2, 0, 0},
    {3, 0, 2},
    {2, 1, 1},
    {0, 0, 2}
  };

  int need[N][M] = {
    {7, 4, 3},
    {1, 2, 2},
    {6, 0, 0},
    {0, 1, 1},
    {4, 3, 1}
  };

  int available[M] = {3, 3, 2};
  int request[M] = {3, 3, 0};
  int pid = 4;

  printf("\n========== TEST 3: UNSAFE REQUEST ==========\n");

  printf("Process P%d requests: %d %d %d\n",
         pid, request[0], request[1], request[2]);

  int valid = 1;

  for(int j = 0; j < M; j++){
    if(request[j] > need[pid][j] ||
       request[j] > available[j]){
      valid = 0;
      break;
    }
  }

  if(!valid){
    printf("Request cannot be granted: exceeds Need or Available.\n");
    return;
  }

  // Provisionally grant the request.
  for(int j = 0; j < M; j++){
    available[j] -= request[j];
    allocation[pid][j] += request[j];
    need[pid][j] -= request[j];
  }

  printf("State after provisional allocation:\n");
  print_matrix("Allocation", allocation);
  print_matrix("Need", need);
  print_available(available);

  if(is_safe_state(allocation, need, available)){
    printf("Request can be granted safely.\n");
  } else {
    printf("Request DENIED: resulting state is unsafe.\n");
  }
}

// Demonstrate circular waiting using two real xv6 processes and pipes.
void
test_real_deadlock(void)
{
  int p1[2], p2[2];

  printf("\n========== TEST 4: REAL PROCESS DEADLOCK ==========\n");

  if(pipe(p1) < 0 || pipe(p2) < 0){
    printf("Pipe creation failed\n");
    exit(1);
  }

  int pid_a = fork();

  if(pid_a < 0){
    printf("fork failed\n");
    exit(1);
  }

  if(pid_a == 0){
    // Process A holds Pipe 2's write end and waits on Pipe 1.
    close(p1[1]);
    close(p2[0]);

    printf("Process A: holds Pipe 2 writer; waiting on Pipe 1...\n");

    char c;
    read(p1[0], &c, 1);

    close(p1[0]);
    close(p2[1]);
    exit(0);
  }

  int pid_b = fork();

  if(pid_b < 0){
    printf("fork failed\n");
    kill(pid_a);
    wait(0);
    exit(1);
  }

  if(pid_b == 0){
    // Process B holds Pipe 1's write end and waits on Pipe 2.
    close(p1[0]);
    close(p2[1]);

    printf("Process B: holds Pipe 1 writer; waiting on Pipe 2...\n");

    char c;
    read(p2[0], &c, 1);

    close(p2[0]);
    close(p1[1]);
    exit(0);
  }

  // The supervisor closes its copies of all pipe ends.
  close(p1[0]);
  close(p1[1]);
  close(p2[0]);
  close(p2[1]);

  // Give both processes time to block on their reads.
  for(volatile int i = 0; i < 10000000; i++)
    ;

  printf("\nBoth processes should now be blocked in circular wait.\n");
  printf("Running deadlock detection on the corresponding resource state...\n");

  int allocation[N][M] = {
    {0, 1, 0},
    {1, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0}
  };

  int request[N][M] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0}
  };

  int available[M] = {0, 0, 0};
  int deadlocked_list[N];

  detect_deadlock(allocation, request, available, deadlocked_list);

  printf("\nTerminating the blocked processes to recover...\n");
  kill(pid_a);
  kill(pid_b);

  wait(0);
  wait(0);

  printf("Real-process deadlock demonstration completed.\n");
}

int
main(void)
{
  printf("Deadlock Detection and Banker's Algorithm\n");

  test_deadlock();
  test_safe();
  test_unsafe_request();
  test_real_deadlock();

  printf("\nAll tests completed.\n");

  exit(0);
}
