package datastore

import "time"

type Pod struct {
	UID       string // Pod UID
	Name      string // Pod Name
	Namespace string // Namespace
	Image     string // Main container image
	IP        string // Pod IP
	OwnerType string // ReplicaSet or nil
	OwnerID   string // ReplicaSet UID
	OwnerName string // ReplicaSet Name
}

type Service struct {
	UID        string
	Name       string
	Namespace  string
	Type       string
	ClusterIP  string
	ClusterIPs []string
	Ports      []struct {
		Src      int32  `json:"src"`
		Dest     int32  `json:"dest"`
		Protocol string `json:"protocol"`
	}
}

type ReplicaSet struct {
	UID       string // ReplicaSet UID
	Name      string // ReplicaSet Name
	Namespace string // Namespace
	OwnerType string // Deployment or nil
	OwnerID   string // Deployment UID
	OwnerName string // Deployment Name
	Replicas  int32  // Number of replicas
}

type Deployment struct {
	UID       string // Deployment UID
	Name      string // Deployment Name
	Namespace string // Namespace
	Replicas  int32  // Number of replicas
}

type Request struct {
	StartTime  time.Time
	Latency    uint64 // in ns
	FromIP     string
	FromType   string
	FromUID    string
	ToIP       string
	ToType     string
	ToUID      string
	Protocol   string
	Completed  bool
	StatusCode uint32
	FailReason string
	Method     string
	Path       string
}
