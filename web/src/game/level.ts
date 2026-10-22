import type { Level } from "./types";

export const level: Level = {
  palette: {
    ink: "#1d1d1d",
    paper: "#fdfdfd",
    blue: "#4ba1f1",
    pink: "#e085f4",
    green: "#4cb05e",
    nodeBlue: "#3266ee",
    nodeLightBlue: "#589be6",
    nodeGrey: "#9da0af",
  },

  start: { pos: { x: 3.2, z: 9.3 }, roomId: "atelier" },

  rooms: [
    { id: "atelier", name: "The Atelier", box: { minX: 0, maxX: 15, minZ: 0, maxZ: 11 } },
    { id: "study", name: "The Study", box: { minX: 8, maxX: 18, minZ: -7, maxZ: 0 } },
  ],

  walls: [
    { a: { x: 0, z: 0 }, b: { x: 10.8, z: 0 } },
    { a: { x: 13.2, z: 0 }, b: { x: 18, z: 0 } },
    { a: { x: 0, z: 0 }, b: { x: 0, z: 11 } },
    { a: { x: 0, z: 11 }, b: { x: 15, z: 11 } },
    { a: { x: 15, z: 0 }, b: { x: 15, z: 11 } },
    { a: { x: 8, z: 0 }, b: { x: 8, z: -7 } },
    { a: { x: 8, z: -7 }, b: { x: 18, z: -7 } },
    { a: { x: 18, z: -7 }, b: { x: 18, z: 0 } },
  ],

  obstacles: [
    { minX: 2.2, maxX: 3.8, minZ: 1.4, maxZ: 3.0 },
    { minX: 14.9, maxX: 16.1, minZ: -5.2, maxZ: -4.0 },
  ],

  doors: [
    {
      id: "study-door",
      name: "study door",
      roomA: "atelier",
      roomB: "study",
      pos: { x: 12, z: 0 },
      hinge: { x: 10.9, z: 0 },
      width: 2.2,
      locked: true,
      keyId: "brass-key",
      throughA: { x: 12, z: 1.7 },
      throughB: { x: 12, z: -1.7 },
    },
  ],

  items: {
    "ink-ribbon": {
      id: "ink-ribbon",
      name: "ink-ribbon",
      description: "A ribbon wound tight with wet blue ink.",
      color: 0xe085f4,
    },
    "green-herb": {
      id: "green-herb",
      name: "green herb",
      description: "Bitter leaves. They clear the fog in your head.",
      color: 0x4cb05e,
    },
    "brass-key": {
      id: "brass-key",
      name: "brass key",
      description: "Small, warm, and cut for the study door.",
      color: 0xd9a441,
    },
  },

  entities: [
    {
      id: "ink-ribbon",
      name: "ink-ribbon",
      kind: "item",
      roomId: "atelier",
      pos: { x: 6, z: 7.6 },
      standAt: { x: 6, z: 6.2 },
      discovery: "room",
      itemId: "ink-ribbon",
    },
    {
      id: "green-herb",
      name: "green herb",
      kind: "item",
      roomId: "atelier",
      pos: { x: 9.2, z: 7.2 },
      standAt: { x: 9.2, z: 5.8 },
      discovery: "room",
      itemId: "green-herb",
    },
    {
      id: "typewriter",
      name: "type-writer",
      kind: "station",
      roomId: "atelier",
      pos: { x: 3, z: 2.3 },
      standAt: { x: 3, z: 3.8 },
      discovery: "room",
      station: "typewriter",
    },
    {
      id: "brass-key",
      name: "brass key",
      kind: "item",
      roomId: "atelier",
      pos: { x: 13.4, z: 1.4 },
      standAt: { x: 13.4, z: 2.6 },
      discovery: "clue",
      discoveryRadius: 2.4,
      itemId: "brass-key",
    },
    {
      id: "study-door",
      name: "study door",
      kind: "door",
      roomId: "atelier",
      pos: { x: 12, z: 0 },
      standAt: { x: 12, z: 1.6 },
      discovery: "proximity",
      discoveryRadius: 3.4,
      doorId: "study-door",
    },
    {
      id: "daybook",
      name: "daybook",
      kind: "station",
      roomId: "study",
      pos: { x: 15.5, z: -4.6 },
      standAt: { x: 15.5, z: -3.3 },
      discovery: "room",
      station: "daybook",
    },
  ],
};

