# MassDsp

基于 Unreal Engine 5 C++ 与 Mass ECS 的工厂模拟 Demo，包含建筑放置、传送带物流、建筑生产、物流塔与无人机调度、存档读档、运行时统计面板等功能。

## 项目演示

演示环境: 14900K+4080Super

演示结果: 在极端测试场景 5w建筑+4w传送带+30w传送带物体+3w物流无人机 下, 平均帧在90FPS附近, 读/存档无明显卡顿

完整视频: [GitHub Release](https://github.com/The-last-pieces/MassDsp/releases/download/1.0.0/Demo.mp4)

![Demo Preview](https://github.com/The-last-pieces/MassDsp/blob/v1/GitDoc/Preview.gif)

![Demo Preview](https://github.com/The-last-pieces/MassDsp/blob/v1/GitDoc/Preview.png)

## 项目简介

MassDsp 是一个以工厂建造与物流运转为核心的技术 Demo，参考了 Dyson Sphere Program 的基础玩法结构，使用 UE5 的 Mass 框架组织大规模实体更新与系统调度。

项目当前已实现以下内容：

- 建筑放置、旋转、拆除
- 传送带连接、物品流转与可视化表现
- 矿机、仓库、合成台、物流塔等基础建筑
- 物流塔供给 / 需求 / 仓储模式
- 无人机运输与归属塔管理
- 建筑交互界面与基础 HUD
- 系统统计面板与模块耗时统计
- 存档与读档恢复
- 科技树与部分解锁逻辑

## 主要特性

### 1. 基于 Mass ECS 的系统组织

建筑运行状态拆分为多个 Fragment，由 Processor 批量更新。  
建筑逻辑、物流逻辑、渲染同步和 UI 展示分别位于不同模块中，便于扩展和维护。

### 2. 传送带物流系统

传送带支持运行时创建、连接、拆除与重建，能够适应LOD自动采样生成不同精度的Mesh。  
物品在传送带上的流转采用逻辑数据与表现数据分离的方式处理，单条传送带逻辑层更新为不考虑物品数量的O(1)时间复杂度，渲染层按需计算物品Transform。

### 3. 物流塔与无人机调度

物流塔可配置为供给、需求或仓储模式。  
无人机由物流子系统统一管理，负责在塔之间执行运输任务，并维护归属塔、任务状态与运行轨迹。

### 4. 存档与读档

项目支持保存和恢复建筑、传送带、库存、生产计时与物流状态。  
读档后会重建运行时数据，并恢复建筑生产与库存状态。

### 5. 运行时调试信息

HUD 提供基础 FPS 与操作提示。  
统计面板可显示建筑数量、物流状态、模块耗时占比等运行信息，用于调试和性能分析。

## 技术实现

### 核心技术栈

- Unreal Engine 5.7
- C++
- UMG
- ESC
- Mass Entity/Representation/LOD
- Instanced Static Mesh Component/Procedural Mesh Component
- WPO Shader

### 系统结构

项目大致分为以下几层：

- 数据层
  - 建筑 Fragment
  - 传送带曲线与物品数据
  - 无人机运行时数据
- 逻辑层
  - 建筑 Processor
  - 物流 Processor
  - 各类 World Subsystem
- 表现层
  - ISM 批量渲染
  - Procedural Mesh 生成传送带网格
  - UI 与 HUD
- 调试层
  - 系统统计快照
  - 模块级 Profile 统计
  - 调试提示信息

## 已实现模块

- 建筑放置与拆除
- 传送带生成、连接与可视化
- 建筑生产流程
- 物流塔模式切换
- 无人机调度与运输
- 玩家背包与部分建筑交互
- 科技树部分解锁逻辑
- 存档与读档
- HUD 与统计面板

### 开发环境

- Unreal Engine 5.7
- Visual Studio 2022
- Windows

## 当前状态

当前版本以工厂基础循环、物流调度、运行时调试能力和底层系统组织为主。  
未覆盖完整游戏内容，仍存在可继续扩展的部分，例如更复杂的物流设备、更多建筑类型、蓝图系统、电网系统和进一步的性能分析工具。

## 说明

该仓库用于展示 UE5 客户端系统开发、Mass ECS 架构组织、运行时物流模拟和性能优化相关实现。
