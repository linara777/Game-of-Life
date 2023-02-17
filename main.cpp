#include "mpi.h"
#include <iostream>
#include <string>
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <ctime>
using namespace std;

#define ind(i, j) ((j) + ((i) * life->board_size))


void* alloc1d(size_t n, size_t size) {
	void* buffer = (void*)malloc(n * size);
	return buffer;
}


typedef struct {
	int board_size;
	int block_size;
	int number_of_iterations;
	int dump_periodicity;


	int* block;

	int rank;
	int size;

	int first_row;
	int last_row;
} life_struct;

void life_init(const char* path, life_struct* life, bool random_init)
{
	int board_size; // Размер поля
	int number_of_iterations;  // Число итераций
	int dump_periodicity;  // Частота сохранения 

	MPI_Comm_size(MPI_COMM_WORLD, &life->size);
	MPI_Comm_rank(MPI_COMM_WORLD, &life->rank);
	ifstream file(path);
	if (!file)
	{
		cout << "Error opening file" << endl;
		MPI_Finalize();
	}

	file >> board_size >> number_of_iterations >> dump_periodicity;
	life->board_size = board_size;
	life->number_of_iterations = number_of_iterations;
	life->dump_periodicity = dump_periodicity;
	life->block_size = board_size / life->size;

	life->block = (int*)alloc1d(board_size * life->block_size, sizeof(int));

	life->first_row = life->rank * life->block_size;
	life->last_row = life->first_row + life->block_size;

	if (random_init) {
		std::srand(std::time(nullptr) * life->rank);
		for (int i = 0; i < life->block_size; i++)
			for (int j = 0; j < board_size; j++)
				life->block[ind(i, j)] = rand() % 2;
	}
	else {
		// Считываем все, но запоминаем только "свои" строчки
		char* temp = (char*)alloc1d(board_size, sizeof(char));
		for (int i = 0; i < board_size; i++) {
			file >> temp;
			if (i >= life->first_row && i < life->last_row)
				for (int j = 0; j < board_size; j++)
					life->block[ind(i - life->first_row, j)] = temp[j] - '0';
		}
		free(temp);
	}
	file.close();

}

void life_step(life_struct* life)
{
	int* todown = (int*)alloc1d(life->board_size, sizeof(int));
	int* toup = (int*)alloc1d(life->board_size, sizeof(int));
	int* fromdown = (int*)alloc1d(life->board_size, sizeof(int));
	int* fromup = (int*)alloc1d(life->board_size, sizeof(int));
	// Необходимо учесть границы соседних блоков

	if (life->rank != life->size - 1) // Если не нижний блок
	{
		for (int j = 0; j < life->board_size; j++)
			todown[j] = life->block[ind(life->block_size - 1, j)];
		// то отправляем ниже последнюю строчку
		MPI_Send(&todown[0], life->board_size, MPI_INT, life->rank + 1, 1, MPI_COMM_WORLD);
	}
	else {
		// если всеже нижний, то считаем еще ниже там нули
		for (int j = 0; j < life->board_size; j++)
			fromdown[j] = 0;
	}

	if (life->rank != 0) // Если не верхний блок
	{
		// получаем сверху строчку
		MPI_Recv(&fromup[0], life->board_size, MPI_INT, life->rank - 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	}
	else {
		// если все же верхний, то сверху только нули
		for (int j = 0; j < life->board_size; j++)
			fromup[j] = 0;
	}

	if (life->rank != 0) // Если не верхний блок
	{
		for (int j = 0; j < life->board_size; j++)
			toup[j] = life->block[ind(0, j)];
		// отправляем верхнюю строчку процессу выше
		MPI_Send(&toup[0], life->board_size, MPI_INT, life->rank - 1, 1, MPI_COMM_WORLD);
	}

	if (life->rank != life->size - 1) // Если не нижний блок
	{
		// Получаем снизу строчку
		MPI_Recv(&fromdown[0], life->board_size, MPI_INT, life->rank + 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	}

	int sum = 0;
	int* board_next_generation = (int*)alloc1d(life->block_size * life->board_size, sizeof(int));
	// Проверяем на границах области
	for (int i = 0; i < life->block_size; i++)
	{
		for (int j = 0; j < life->board_size; j++)
		{
			if (i == 0 && j == 0) // верхний левый угол
				sum = life->block[ind(i + 1, j)] + life->block[ind(i + 1, j + 1)] + life->block[ind(i, j + 1)] + fromup[0] + fromup[1];
			else if (i == 0 && j == life->board_size - 1) // верхний правый угол
				sum = life->block[ind(i, j - 1)] + life->block[ind(i + 1, j - 1)] + life->block[ind(i + 1, j)] + fromup[life->board_size - 1] + fromup[life->board_size - 2];
			else if (i == life->block_size - 1 && j == 0) // нижный левый угол
				sum = life->block[ind(i, j + 1)] + life->block[ind(i - 1, j + 1)] + life->block[ind(i - 1, j)] + fromdown[0] + fromdown[1];
			else if (i == life->block_size - 1 && j == life->board_size - 1) // нижный правый угол
				sum = life->block[ind(i - 1, j)] + life->block[ind(i - 1, j - 1)] + life->block[ind(i, j - 1)] + fromdown[life->board_size - 1] + fromdown[life->board_size - 2];
			else
			{
				if (j == 0) // левый столбец
					sum = life->block[ind(i - 1, j)] + life->block[ind(i - 1, j + 1)] + life->block[ind(i, j + 1)] + life->block[ind(i + 1, j + 1)] + life->block[ind(i + 1, j)];
				else if (j == life->board_size - 1) // правый столбец
					sum = life->block[ind(i - 1, j)] + life->block[ind(i - 1, j - 1)] + life->block[ind(i, j - 1)] + life->block[ind(i + 1, j - 1)] + life->block[ind(i + 1, j)];
				else if (i == 0) // верхняя линия
					sum = life->block[ind(i, j - 1)] + life->block[ind(i + 1, j - 1)] + life->block[ind(i + 1, j)] + life->block[ind(i + 1, j + 1)] + life->block[ind(i, j + 1)] + fromup[j - 1] + fromup[j] + fromup[j + 1];
				else if (i == life->block_size - 1) // нижняя линия
					sum = life->block[ind(i - 1, j - 1)] + life->block[ind(i - 1, j)] + life->block[ind(i - 1, j + 1)] + life->block[ind(i, j + 1)] + life->block[ind(i, j - 1)] + fromdown[j - 1] + fromdown[j] + fromdown[j + 1];
				else // неграничный случай
					sum = life->block[ind(i - 1, j - 1)] + life->block[ind(i - 1, j)] + life->block[ind(i - 1, j + 1)] + life->block[ind(i, j + 1)] + life->block[ind(i + 1, j + 1)] + life->block[ind(i + 1, j)] + life->block[ind(i + 1, j - 1)] + life->block[ind(i, j - 1)];
			}

			// применяем правило игры
			if (life->block[ind(i, j)] == 1 && (sum == 2 || sum == 3)) board_next_generation[ind(i, j)] = 1;
			else if (life->block[ind(i, j)] == 1 && sum > 3) board_next_generation[ind(i, j)] = 0;
			else if (life->block[ind(i, j)] == 1 && sum < 1) board_next_generation[ind(i, j)] = 0;
			else if (life->block[ind(i, j)] == 0 && sum == 3) board_next_generation[ind(i, j)] = 1;
			else board_next_generation[ind(i, j)] = 0;

		}
	}

	for (int i = 0; i < life->block_size; i++)
		for (int j = 0; j < life->board_size; j++)
			life->block[ind(i, j)] = board_next_generation[ind(i, j)];
	free(board_next_generation);
	free(toup);
	free(todown);
	free(fromup);
	free(fromdown);
}

void save_generation(char* path, life_struct* life, int epoch)
{

	// Пишет только главный процесс, остальные отправляют ему
	int* block = (int*)alloc1d(life->block_size * life->board_size, sizeof(int));

	if (life->rank == 0)
	{

		for (int process = 0; process < life->size; process++)
		{
			if (process != 0)
				// Получаем посчитанные области других процессов
				MPI_Recv(&block[0], life->board_size * life->block_size, MPI_INT, process, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			else {
				// От себя не получаем, а кладем в буффер свой кусок данных
				for (int k = 0; k < life->block_size * life->board_size; k++)
					block[k] = life->block[k];
			}

		}

	}
	else {
		MPI_Send(&life->block[0], life->board_size * life->block_size, MPI_INT, 0, 1, MPI_COMM_WORLD);
	}
	free(block);
}

int main(int argc, char* argv[])
{

	MPI_Status Stat;
	int rc;
	rc = MPI_Init(&argc, &argv);
	if (rc != 0)
	{
		cout << "Error starting MPI." << endl;
		MPI_Abort(MPI_COMM_WORLD, rc);
	}
	if (argc < 2)
	{
		cout << "Input file not specified" << endl;
		MPI_Finalize();
		return -1;
	}
	double init_time = MPI_Wtime();

	life_struct life;
	bool random_init = false;
	if (argc == 3)
		random_init = true;
	else if (argc > 3) {
		cout << "Too many arguments" << endl;
		MPI_Finalize();
		return -1;
	}

	life_init(argv[1], &life, random_init);



	for (int epoch = 1; epoch <= life.number_of_iterations; epoch++)
	{
		life_step(&life);
		if (epoch % life.dump_periodicity == 0)
		{
			char buff[50];
		
			save_generation(buff, &life, epoch);
		}
	}

	double final_time = MPI_Wtime() - init_time;
	double perf = 1e-6 * life.board_size * life.board_size * life.number_of_iterations / final_time;
	if (life.rank == 0) {
		cout << endl;
		cout << "Number of workers: " << life.size << ", time: " << final_time << ", perf: " << perf << " MFLOPS\n" << endl;
	}

	free(life.block);
	MPI_Finalize();
}