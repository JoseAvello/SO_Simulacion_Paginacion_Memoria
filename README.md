# Simulador de Paginación – Tarea 3 (Sistemas Operativos) - JA - AV

## 1. Descripción General

Este programa implementa una simulación del mecanismo de **paginación de memoria**, tal como funciona en un sistema operativo real. Permite observar el comportamiento de la RAM, la memoria virtual, el área de swap, la creación y finalización de procesos, los accesos a direcciones virtuales, los page faults y el reemplazo de páginas.

La simulación opera completamente a nivel lógico: no se almacena contenido real en memoria, solo estructuras de metadatos que representan páginas, procesos y frames.

---

## 2. Compilación

El programa debe compilarse en un ambiente **UNIX/Linux** utilizando GCC:

```bash
gcc -std=c11 -O2 -o sim_paginacion_fifo tarea.c
```

---

## 3. Ejecución

Para iniciar la simulación:

```bash
./sim_paginacion_fifo
```

El programa solicitará:

1. Tamaño de memoria física (MB)  
2. Tamaño de página (KB)  
3. Tamaño mínimo de proceso (KB)  
4. Tamaño máximo de proceso (KB)  

Ejemplo de entrada:

```
10
4
500
1500
```

---

## 4. Funcionamiento de la Simulación

### 4.1 Creación de procesos (cada 2 segundos)

- Se crea un proceso nuevo con tamaño aleatorio.
- Se calcula cuántas páginas requiere.
- Se intenta asignar RAM; si no hay espacio, pasa a swap.
- Si no queda memoria en RAM ni swap, la simulación termina.

### 4.2 Acceso a direcciones virtuales (cada 5 segundos desde el segundo 30)

- Se genera una dirección virtual aleatoria.
- Se determina la página asociada.
- Si no está en RAM se produce un **page fault**.
- Si RAM está llena, se reemplaza una página mediante FIFO.

### 4.3 Finalización de procesos (cada 5 segundos desde el segundo 30)

- Se selecciona un proceso aleatorio.
- Se liberan sus páginas en RAM y swap.

---

## 5. Política de Reemplazo (FIFO)

El simulador utiliza **FIFO (First In – First Out)**:

- Cada página cargada a RAM se encola.
- Cuando ocurre un page fault y RAM está completa:
  - Se expulsa la página más antigua.
  - Esta pasa a swap.
  - La nueva página ocupa su frame.

---

## 6. Condiciones de Término

La simulación finaliza automáticamente cuando:

- No quedan frames libres
- No hay slots disponibles en swap.

---

## 7. Estructuras Principales

- **Process**: ID, tamaño y páginas.  
- **Page**: ubicación (RAM o swap), frame o slot.  
- **Frame**: representa un marco de memoria física.  
- **SwapSlot**: representa una posición en el área de swap.  
- **FIFO Queue**: cola para administrar el reemplazo.

---