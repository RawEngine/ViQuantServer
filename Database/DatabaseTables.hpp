
#pragma once

namespace Database
{
	namespace Table
	{
		namespace Events
		{
			constexpr auto TableName	= "`vq_events`";
			constexpr auto Id			= "`id`";
			constexpr auto UserId		= "`user_id`";
			constexpr auto SiteId		= "`site_id`";
			constexpr auto CameraId		= "`camera_id`";
			constexpr auto CreatedAt	= "`created_at`";
			constexpr auto EndedAt		= "`ended_at`";
		}

		namespace Sites
		{
			constexpr auto TableName	= "`vq_sites`";
			constexpr auto Id			= "`id`";
			constexpr auto UserId		= "`user_id`";
			constexpr auto IsArmed		= "`is_armed`";
			constexpr auto DeletedAt	= "`deleted_at`";
		}

		namespace Users
		{
			constexpr auto TableName	= "`vq_users`";
			constexpr auto Id			= "`id`";
			constexpr auto IsActive		= "`is_active`";
		}

		namespace Cameras
		{
			constexpr auto TableName		= "`vq_cameras`";
			constexpr auto Id				= "`id`";
			constexpr auto SiteId			= "`site_id`";
			constexpr auto IsArmed			= "`is_armed`";
			constexpr auto PersonThreshold	= "`person_threshold`";
			constexpr auto DeletedAt		= "`deleted_at`";
		}

		namespace CameraFTP
		{
			constexpr auto TableName	= "`vq_camera_ftp`";
			constexpr auto Id			= "`id`";
			constexpr auto CameraId		= "`camera_id`";
			constexpr auto Username		= "`username`";
			constexpr auto Password		= "`password`";
//			constexpr auto Timeout		= "`timeout`"; // In seconds. TODO - REMOVE.
		};

		namespace CameraDetections
		{
			constexpr auto TableName	= "`vq_cameras_detections`";
			constexpr auto CameraId		= "`camera_id`";
			constexpr auto Name			= "`name`";
			constexpr auto Count		= "`count`";
		};

		//update vq_cameras_detections set count = count + 1 where camera_id = [cameraID] and name = 'person'

		namespace Footage
		{
			constexpr auto TableName	= "`vq_event_footage`";
			constexpr auto Id			= "`id`";
			constexpr auto EventId		= "`event_id`";
			constexpr auto Name			= "`name`";
			constexpr auto Timestamp	= "`timestamp`";	// If possible-retrieved from the filename. Else, current server timestamp will be used.
			constexpr auto Milliseconds	= "`ms`";			// Milliseconds for the timestamp. (created_at)
			constexpr auto CreatedAt	= "`created_at`";
		};

		namespace Analytics
		{
			constexpr auto TableName		= "`vq_event_analytics`";
			constexpr auto Id				= "`id`";
			constexpr auto EventFootageId	= "`event_footage_id`";
			constexpr auto Frame			= "`frame`";
			constexpr auto Type				= "`type`";
			constexpr auto Probability		= "`probability`";
			constexpr auto X				= "`x`";
			constexpr auto Y				= "`y`";
			constexpr auto W				= "`w`";
			constexpr auto H				= "`h`";
		}

		namespace AnalyticsXML
		{
			constexpr auto TableName		= "`vq_event_analytics_xml`";
			constexpr auto Id				= "`id`";
			constexpr auto EventFootageId	= "`event_footage_id`";
			constexpr auto Data				= "`data`";
			constexpr auto CreatedAt		= "`created_at`";
		}
	}
}

/*

ALTER TABLE `vq_event_footage` ADD COLUMN `ms` SMALLINT UNSIGNED NOT NULL COMMENT 'Milliseconds for the timestamp. (created_at)' AFTER `height`, DROP COLUMN `ms`;

ALTER TABLE `vq_event_footage` ADD COLUMN `timestamp` TIMESTAMP NULL COMMENT 'If possible-retrieved from the filename. Else, current server timestamp will be used.' AFTER `ms`;

ALTER TABLE `vq_event_footage` CHANGE COLUMN `created_at` `created_at` TIMESTAMP NULL DEFAULT CURRENT_TIMESTAMP COMMENT 'Timestamp of when this record was added.' AFTER `timestamp`;

*/
