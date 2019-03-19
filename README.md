# Concurrency Game

Small concurrency game where processes try to kill each other.

This is a turn-based game when at each turn a process decide to attack or defend randomly. If a process attack another process that is also attacking, then the attack will be successful and the second process will die. If the second process is defending, it will survive the turn. The game finishes when only one or two processes remain. If two processes survive, this will be a tie. Otherwise, we will have a clear winner.

This game is part of the "Operating Systems Design and Administration" course of the BSc in Computer Science at UNED.

