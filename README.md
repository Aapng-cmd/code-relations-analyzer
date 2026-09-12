# CoReAnalyzer

## Описание

Сделано для анализа легаси проектов, в которых надо понять, что на чём строится.  
Визуализирует связи между файлами и показывает, какие классы/функции/переменные были использованы. Работает с проектами на Python и C/C++.

## Начало работы

### Зависимости

* CMake
* C++

### Установка

```bash
git clone https://github.com/Aapng-cmd/code-relations-analyzer.git
cd code-relations-analyzer/
mkdir build && cd build
cmake .. && cmake --build .
```

### Запуск программы

```bash
./build/code-relations-analyzer
```

## История версий

* 0.1
    * Первоначальный выпуск
